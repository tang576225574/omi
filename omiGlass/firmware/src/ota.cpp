/**
 * OTA固件升级模块
 *
 * 负责通过WiFi下载和安装新固件
 * 主要功能:
 * - 接收BLE命令配置WiFi和固件URL
 * - 连接WiFi网络
 * - 从HTTP/HTTPS下载固件
 * - 写入Flash并重启
 * - 实时进度通知
 *
 * OTA流程:
 * 1. 客户端通过BLE发送WiFi凭据(SSID和密码)
 * 2. 客户端通过BLE发送固件URL
 * 3. 客户端发送START命令启动OTA
 * 4. 固件连接WiFi → 下载固件 → 写入Flash → 重启
 *
 * 安全特性:
 * - 支持HTTPS(跳过证书验证,生产环境需添加证书)
 * - 分区表支持OTA(使用双分区切换)
 * - 可随时取消升级
 */
#include "ota.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <BLE2902.h>

// ============================================================================
// 全局状态变量
// ============================================================================

// OTA状态
static uint8_t otaStatus = OTA_STATUS_IDLE;     // 当前OTA状态码
static uint8_t otaProgress = 0;                 // 当前进度(0-100)
static bool otaCancelled = false;               // 取消标志

// WiFi凭据(临时存储)
static char wifiSSID[WIFI_MAX_SSID_LEN + 1] = {0};       // WiFi SSID(最大32字符)
static char wifiPassword[WIFI_MAX_PASS_LEN + 1] = {0};   // WiFi密码(最大64字符)
static bool wifiCredentialsSet = false;                  // WiFi凭据是否已设置

// 固件URL
static char firmwareURL[OTA_MAX_URL_LEN + 1] = {0};      // 固件下载URL(最大256字符)
static bool firmwareURLSet = false;                      // URL是否已设置

// OTA任务状态
static bool otaTaskRunning = false;              // OTA任务是否正在运行
static TaskHandle_t otaTaskHandle = NULL;        // OTA任务句柄

// BLE特性指针
static BLECharacteristic *otaControlCharacteristic = NULL;  // OTA控制特性
static BLECharacteristic *otaDataCharacteristic = NULL;     // OTA数据特性(进度通知)

// ============================================================================
// 内部函数前向声明
// ============================================================================
static void ota_task(void *parameter);           // OTA任务主函数
static bool connect_wifi();                      // 连接WiFi
static bool download_and_install_firmware();     // 下载并安装固件

// ============================================================================
// BLE回调类
// ============================================================================

/**
 * OTAControlCallback - OTA控制特性回调
 *
 * 处理客户端对OTA控制特性的读写操作
 */
class OTAControlCallback : public BLECharacteristicCallbacks {
    /**
     * onWrite - 客户端写入OTA命令时调用
     *
     * 解析命令并调用ota_handle_command处理
     */
    void onWrite(BLECharacteristic *pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            ota_handle_command((uint8_t *)value.data(), value.length());
        }
    }

    /**
     * onRead - 客户端读取OTA状态时调用
     *
     * 返回当前OTA状态和进度(2字节)
     */
    void onRead(BLECharacteristic *pCharacteristic) override {
        // 返回当前状态和进度
        uint8_t status[2] = {otaStatus, otaProgress};
        pCharacteristic->setValue(status, 2);
    }
};

// ============================================================================
// 公开API函数
// ============================================================================

/**
 * ota_set_characteristics - 设置OTA BLE特性
 *
 * @param {BLECharacteristic*} controlChar - 控制特性指针
 * @param {BLECharacteristic*} dataChar - 数据特性指针
 *
 * 由app.cpp在BLE初始化时调用
 */
void ota_set_characteristics(BLECharacteristic *controlChar, BLECharacteristic *dataChar) {
    otaControlCharacteristic = controlChar;
    otaDataCharacteristic = dataChar;
}

/**
 * ota_handle_command - 处理OTA命令
 *
 * @param {uint8_t*} data - 命令数据
 * @param {size_t} length - 数据长度
 *
 * 支持的命令:
 * - OTA_CMD_SET_WIFI (0x01): 设置WiFi凭据
 *   格式: [cmd, ssid_len, ssid..., pass_len, pass...]
 *
 * - OTA_CMD_SET_URL (0x05): 设置固件URL
 *   格式: [cmd, url_len_high, url_len_low, url...]
 *
 * - OTA_CMD_START_OTA (0x02): 启动OTA升级
 *   格式: [cmd]
 *
 * - OTA_CMD_CANCEL_OTA (0x03): 取消OTA
 *   格式: [cmd]
 *
 * - OTA_CMD_GET_STATUS (0x04): 查询状态
 *   格式: [cmd]
 */
void ota_handle_command(uint8_t *data, size_t length) {
    if (length < 1) return;

    uint8_t command = data[0];
    Serial.printf("OTA: Received command 0x%02X, length %d\n", command, length);

    switch (command) {
        case OTA_CMD_SET_WIFI: {
            // 设置WiFi凭据
            // 格式: [cmd, ssid_len, ssid..., pass_len, pass...]
            if (length < 3) {
                Serial.println("OTA: Invalid WiFi command length");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            // 解析SSID长度
            uint8_t ssidLen = data[1];
            if (ssidLen > WIFI_MAX_SSID_LEN || length < 3 + ssidLen) {
                Serial.println("OTA: Invalid SSID length");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            // 复制SSID
            memcpy(wifiSSID, &data[2], ssidLen);
            wifiSSID[ssidLen] = '\0';

            // 解析密码长度
            uint8_t passLen = data[2 + ssidLen];
            if (passLen > WIFI_MAX_PASS_LEN || length < 3 + ssidLen + passLen) {
                Serial.println("OTA: Invalid password length");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            // 复制密码
            memcpy(wifiPassword, &data[3 + ssidLen], passLen);
            wifiPassword[passLen] = '\0';

            wifiCredentialsSet = true;
            Serial.printf("OTA: WiFi credentials set - SSID: %s\n", wifiSSID);
            ota_notify_status(OTA_STATUS_IDLE);
            break;
        }

        case OTA_CMD_SET_URL: {
            // 设置固件URL
            // 格式: [cmd, url_len_high, url_len_low, url...]
            if (length < 4) {
                Serial.println("OTA: Invalid URL command length");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            // 解析URL长度(大端序,2字节)
            uint16_t urlLen = (data[1] << 8) | data[2];
            if (urlLen > OTA_MAX_URL_LEN || length < 3 + urlLen) {
                Serial.println("OTA: Invalid URL length");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            // 复制URL
            memcpy(firmwareURL, &data[3], urlLen);
            firmwareURL[urlLen] = '\0';

            firmwareURLSet = true;
            Serial.printf("OTA: Firmware URL set: %s\n", firmwareURL);
            ota_notify_status(OTA_STATUS_IDLE);
            break;
        }

        case OTA_CMD_START_OTA: {
            // 启动OTA升级
            // 检查前置条件
            if (!wifiCredentialsSet) {
                Serial.println("OTA: WiFi credentials not set");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            if (!firmwareURLSet) {
                Serial.println("OTA: Firmware URL not set");
                ota_notify_status(OTA_STATUS_ERROR);
                return;
            }

            if (otaTaskRunning) {
                Serial.println("OTA: Update already in progress");
                return;
            }

            // 创建OTA任务
            otaCancelled = false;
            otaTaskRunning = true;
            // 参数: 任务函数, 任务名, 栈大小8KB, 参数, 优先级5, 任务句柄
            xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, &otaTaskHandle);
            break;
        }

        case OTA_CMD_CANCEL_OTA: {
            // 取消OTA
            ota_cancel();
            break;
        }

        case OTA_CMD_GET_STATUS: {
            // 查询状态
            ota_notify_status(otaStatus, otaProgress);
            break;
        }

        default:
            Serial.printf("OTA: Unknown command 0x%02X\n", command);
            ota_notify_status(OTA_STATUS_ERROR);
            break;
    }
}

/**
 * ota_notify_status - 通知OTA状态
 *
 * @param {uint8_t} status - 状态码
 * @param {uint8_t} progress - 进度(0-100)
 *
 * 更新全局状态并通过BLE通知客户端
 *
 * 状态码定义(config.h):
 * - 0x00: IDLE - 空闲
 * - 0x10: WIFI_CONNECTING - 正在连接WiFi
 * - 0x11: WIFI_CONNECTED - WiFi已连接
 * - 0x12: WIFI_FAILED - WiFi连接失败
 * - 0x20: DOWNLOADING - 正在下载(附带进度)
 * - 0x21: DOWNLOAD_COMPLETE - 下载完成
 * - 0x22: DOWNLOAD_FAILED - 下载失败
 * - 0x30: INSTALLING - 正在安装(附带进度)
 * - 0x31: INSTALL_COMPLETE - 安装完成
 * - 0x32: INSTALL_FAILED - 安装失败
 * - 0x40: REBOOTING - 正在重启
 * - 0xFF: ERROR - 错误
 */
void ota_notify_status(uint8_t status, uint8_t progress) {
    otaStatus = status;
    otaProgress = progress;

    // 通过BLE发送通知
    if (otaDataCharacteristic != NULL) {
        uint8_t notification[2] = {status, progress};
        otaDataCharacteristic->setValue(notification, 2);
        otaDataCharacteristic->notify();
    }

    Serial.printf("OTA: Status 0x%02X, Progress %d%%\n", status, progress);
}

// ============================================================================
// OTA任务主函数
// ============================================================================

/**
 * ota_task - OTA任务主函数
 *
 * @param {void*} parameter - 任务参数(未使用)
 *
 * FreeRTOS任务,在独立线程中执行OTA流程:
 * 1. 连接WiFi
 * 2. 下载固件
 * 3. 写入Flash
 * 4. 重启系统
 *
 * 任务完成后自动删除
 */
static void ota_task(void *parameter) {
    Serial.println("OTA: Task started");

    // ========================================================================
    // 步骤1: 连接WiFi
    // ========================================================================
    if (!connect_wifi()) {
        ota_notify_status(OTA_STATUS_WIFI_FAILED);
        otaTaskRunning = false;
        vTaskDelete(NULL); // 删除任务
        return;
    }

    // 检查是否被取消
    if (otaCancelled) {
        WiFi.disconnect(true);
        ota_notify_status(OTA_STATUS_IDLE);
        otaTaskRunning = false;
        vTaskDelete(NULL);
        return;
    }

    // ========================================================================
    // 步骤2: 下载并安装固件
    // ========================================================================
    if (!download_and_install_firmware()) {
        WiFi.disconnect(true);
        otaTaskRunning = false;
        vTaskDelete(NULL);
        return;
    }

    // ========================================================================
    // 步骤3: 准备重启
    // ========================================================================
    Serial.println("OTA: Preparing to reboot...");
    ota_notify_status(OTA_STATUS_REBOOTING);
    delay(2000);  // 给BLE通知留出发送时间

    Serial.println("OTA: Disconnecting WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);

    Serial.println("OTA: Rebooting now!");
    ESP.restart(); // 重启系统,启动新固件

    // 不应到达这里
    vTaskDelete(NULL);
}

// ============================================================================
// WiFi连接函数
// ============================================================================

/**
 * connect_wifi - 连接WiFi网络
 *
 * @returns {bool} 成功返回true,失败返回false
 *
 * 功能说明:
 * - 使用STA模式连接WiFi
 * - 超时时间15秒(WIFI_CONNECT_TIMEOUT_MS)
 * - 支持取消操作
 * - 连接成功后打印IP地址
 */
static bool connect_wifi() {
    Serial.printf("OTA: Connecting to WiFi: %s\n", wifiSSID);
    ota_notify_status(OTA_STATUS_WIFI_CONNECTING);

    // 配置WiFi为Station模式
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        // 检查是否被取消
        if (otaCancelled) {
            Serial.println("OTA: WiFi connection cancelled");
            return false;
        }

        // 检查超时(15秒)
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("OTA: WiFi connection timeout");
            return false;
        }

        delay(500);
        Serial.print(".");
    }

    Serial.printf("\nOTA: WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    ota_notify_status(OTA_STATUS_WIFI_CONNECTED);
    return true;
}

// ============================================================================
// 固件下载和安装函数
// ============================================================================

/**
 * download_and_install_firmware - 下载并安装固件
 *
 * @returns {bool} 成功返回true,失败返回false
 *
 * 功能说明:
 * 1. 判断URL是HTTP还是HTTPS
 * 2. 创建对应的WiFi客户端
 * 3. 发送HTTP GET请求
 * 4. 检查响应码和内容长度
 * 5. 初始化OTA更新(检查Flash空间)
 * 6. 流式下载固件并写入Flash
 * 7. 每5%进度通知一次
 * 8. 验证下载完整性
 * 9. 完成OTA更新
 *
 * 安全特性:
 * - 支持HTTPS(setInsecure跳过证书验证)
 * - TODO: 生产环境应添加证书验证
 * - 支持HTTP重定向
 * - 30秒超时保护
 * - 可随时取消
 */
static bool download_and_install_firmware() {
    Serial.printf("OTA: Downloading firmware from: %s\n", firmwareURL);
    ota_notify_status(OTA_STATUS_DOWNLOADING, 0);

    // ========================================================================
    // 1. 判断URL类型并创建客户端
    // ========================================================================
    bool isHttps = strncmp(firmwareURL, "https://", 8) == 0;
    Serial.printf("OTA: Using %s\n", isHttps ? "HTTPS" : "HTTP");

    WiFiClient *client = nullptr;
    WiFiClientSecure *secureClient = nullptr;

    if (isHttps) {
        // 创建HTTPS客户端
        secureClient = new WiFiClientSecure;
        if (!secureClient) {
            Serial.println("OTA: Failed to create WiFiClientSecure");
            ota_notify_status(OTA_STATUS_DOWNLOAD_FAILED);
            return false;
        }
        // 跳过证书验证(测试用,生产环境应添加证书)
        secureClient->setInsecure();
        client = secureClient;
    } else {
        // 创建HTTP客户端
        client = new WiFiClient;
        if (!client) {
            Serial.println("OTA: Failed to create WiFiClient");
            ota_notify_status(OTA_STATUS_DOWNLOAD_FAILED);
            return false;
        }
    }

    // ========================================================================
    // 2. 配置HTTP客户端
    // ========================================================================
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // 严格跟随重定向
    http.begin(*client, firmwareURL);
    http.setTimeout(30000);  // 30秒超时
    http.addHeader("User-Agent", "ESP32-OTA/1.0");

    // ========================================================================
    // 3. 发送GET请求
    // ========================================================================
    Serial.printf("OTA: Starting HTTP GET request...\n");
    int httpCode = http.GET();
    Serial.printf("OTA: HTTP response code: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("OTA: HTTP GET failed, code: %d\n", httpCode);
        ota_notify_status(OTA_STATUS_DOWNLOAD_FAILED);
        http.end();
        if (secureClient) delete secureClient; else delete client;
        return false;
    }

    // ========================================================================
    // 4. 检查内容长度
    // ========================================================================
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("OTA: Invalid content length");
        ota_notify_status(OTA_STATUS_DOWNLOAD_FAILED);
        http.end();
        if (secureClient) delete secureClient; else delete client;
        return false;
    }

    Serial.printf("OTA: Firmware size: %d bytes\n", contentLength);

    // ========================================================================
    // 5. 初始化OTA更新(检查Flash空间)
    // ========================================================================
    if (!Update.begin(contentLength)) {
        Serial.println("OTA: Not enough space for update");
        ota_notify_status(OTA_STATUS_INSTALL_FAILED);
        http.end();
        if (secureClient) delete secureClient; else delete client;
        return false;
    }

    // ========================================================================
    // 6. 流式下载并写入Flash
    // ========================================================================
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[1024];  // 1KB缓冲区
    int totalRead = 0;
    int lastProgress = -1;

    ota_notify_status(OTA_STATUS_INSTALLING, 0);

    while (http.connected() && totalRead < contentLength) {
        // 检查是否被取消
        if (otaCancelled) {
            Serial.println("OTA: Download cancelled");
            Update.abort();
            http.end();
            if (secureClient) delete secureClient; else delete client;
            ota_notify_status(OTA_STATUS_IDLE);
            return false;
        }

        size_t available = stream->available();
        if (available > 0) {
            // 读取数据
            size_t toRead = min(available, sizeof(buffer));
            int bytesRead = stream->readBytes(buffer, toRead);

            if (bytesRead > 0) {
                // 写入Flash
                if (Update.write(buffer, bytesRead) != bytesRead) {
                    Serial.println("OTA: Write failed");
                    Update.abort();
                    http.end();
                    if (secureClient) delete secureClient; else delete client;
                    ota_notify_status(OTA_STATUS_INSTALL_FAILED);
                    return false;
                }

                totalRead += bytesRead;
                int progress = (totalRead * 100) / contentLength;

                // 每5%通知一次进度
                if (progress != lastProgress && progress % 5 == 0) {
                    ota_notify_status(OTA_STATUS_INSTALLING, progress);
                    lastProgress = progress;
                }
            }
        } else {
            delay(10); // 等待更多数据
        }
    }

    http.end();

    // 释放客户端内存
    if (secureClient) {
        delete secureClient;
    } else {
        delete client;
    }

    // ========================================================================
    // 7. 验证下载完整性
    // ========================================================================
    if (totalRead != contentLength) {
        Serial.printf("OTA: Incomplete download: %d/%d\n", totalRead, contentLength);
        Update.abort();
        ota_notify_status(OTA_STATUS_DOWNLOAD_FAILED);
        return false;
    }

    // ========================================================================
    // 8. 完成OTA更新
    // ========================================================================
    if (!Update.end(true)) {
        Serial.printf("OTA: Update failed: %s\n", Update.errorString());
        ota_notify_status(OTA_STATUS_INSTALL_FAILED);
        return false;
    }

    Serial.println("OTA: Update complete!");
    ota_notify_status(OTA_STATUS_INSTALL_COMPLETE, 100);
    delay(500);  // 给BLE时间发送通知
    return true;
}

// ============================================================================
// 其他公开API函数
// ============================================================================

/**
 * ota_loop - OTA主循环函数
 *
 * 在主循环中定期调用
 * 当前不需要执行任何操作,因为OTA在独立任务中运行
 */
void ota_loop() {
    // 当前不需要在循环中做任何事 - OTA在独立任务中运行
}

/**
 * ota_get_status - 获取当前OTA状态
 *
 * @returns {uint8_t} 当前状态码
 */
uint8_t ota_get_status() {
    return otaStatus;
}

/**
 * ota_is_busy - 检查OTA是否正在运行
 *
 * @returns {bool} 正在运行返回true,否则返回false
 */
bool ota_is_busy() {
    return otaTaskRunning;
}

/**
 * ota_cancel - 取消OTA升级
 *
 * 设置取消标志,OTA任务会在检查点退出
 */
void ota_cancel() {
    if (otaTaskRunning) {
        Serial.println("OTA: Cancelling...");
        otaCancelled = true;
    }
}
