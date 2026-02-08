/**
 * 应用主逻辑文件 - omiGlass智能眼镜固件
 *
 * 这是固件的核心文件,负责协调所有子系统:
 * - BLE通信(OMI协议)
 * - 相机拍照和传输
 * - 音频采集和编码
 * - 电源管理和电池监控
 * - 按钮和LED控制
 * - OTA固件升级
 *
 * 硬件平台: XIAO ESP32-S3 Sense
 * 固件版本: 2.3.2
 */
#include "app.h"

// BLE库
#include <BLE2902.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>

// 系统库
#include "config.h"        // 所有配置参数
#include "esp_camera.h"    // ESP32相机驱动
#include "esp_sleep.h"     // 电源管理(睡眠模式)
#include "mic.h"           // 麦克风I2S驱动
#include "opus_encoder.h"  // Opus音频编码器
#include "ota.h"           // OTA固件升级

// ============================================================================
// 全局状态变量
// ============================================================================

// 电池状态
float batteryVoltage = 0.0f;           // 当前电池电压(V)
int batteryPercentage = 0;             // 当前电池电量百分比(0-100)
unsigned long lastBatteryCheck = 0;    // 上次电池检查时间戳(ms)

// 设备电源状态
bool deviceActive = true;                    // 设备是否活跃
device_state_t deviceState = DEVICE_BOOTING; // 设备状态机

// 按钮和LED状态
volatile bool buttonPressed = false;    // 按钮按下标志(在ISR中设置)
unsigned long buttonPressTime = 0;      // 按钮按下时间戳
led_status_t ledMode = LED_BOOT_SEQUENCE; // LED模式(启动/正常/关机)

// 电源优化相关
unsigned long lastActivity = 0;  // 上次活动时间戳(用于判断是否进入省电模式)
bool powerSaveMode = false;      // 省电模式标志

// 轻度睡眠优化 - 节省约15mA电流,增加3-4小时续航
bool lightSleepEnabled = true;

// ============================================================================
// BLE服务和特性定义
// ============================================================================

// 标准设备信息服务UUID(Bluetooth SIG定义)
#define DEVICE_INFORMATION_SERVICE_UUID (uint16_t) 0x180A        // 设备信息服务
#define MANUFACTURER_NAME_STRING_CHAR_UUID (uint16_t) 0x2A29    // 制造商名称
#define MODEL_NUMBER_STRING_CHAR_UUID (uint16_t) 0x2A24         // 型号
#define FIRMWARE_REVISION_STRING_CHAR_UUID (uint16_t) 0x2A26    // 固件版本
#define HARDWARE_REVISION_STRING_CHAR_UUID (uint16_t) 0x2A27    // 硬件版本
#define SERIAL_NUMBER_STRING_CHAR_UUID (uint16_t) 0x2A25        // 序列号

// OMI自定义服务UUID(从config.h获取)
static BLEUUID serviceUUID(OMI_SERVICE_UUID);       // 主服务
static BLEUUID photoDataUUID(PHOTO_DATA_UUID);      // 照片数据特性
static BLEUUID photoControlUUID(PHOTO_CONTROL_UUID); // 照片控制特性
static BLEUUID audioDataUUID(AUDIO_DATA_UUID);      // 音频数据特性
static BLEUUID audioCodecUUID(AUDIO_CODEC_UUID);    // 音频编解码器ID特性

// OTA服务UUID
static BLEUUID otaServiceUUID(OTA_SERVICE_UUID);    // OTA服务
static BLEUUID otaControlUUID(OTA_CONTROL_UUID);    // OTA控制特性
static BLEUUID otaDataUUID(OTA_DATA_UUID);          // OTA数据特性

// BLE特性指针(用于读写和通知)
BLECharacteristic *photoDataCharacteristic;      // 照片数据
BLECharacteristic *photoControlCharacteristic;   // 照片控制
BLECharacteristic *batteryLevelCharacteristic;   // 电池电量
BLECharacteristic *audioDataCharacteristic;      // 音频数据
BLECharacteristic *audioCodecCharacteristic;     // 音频编解码器
BLECharacteristic *otaControlCharacteristic;     // OTA控制
BLECharacteristic *otaDataCharacteristic;        // OTA数据

// ============================================================================
// 音频状态
// ============================================================================
bool audioEnabled = true;              // 音频功能是否启用
volatile bool audioSubscribed = false; // 客户端是否订阅了音频通知
uint16_t audioPacketIndex = 0;         // 音频包序号(用于重组)

// ============================================================================
// 连接和拍照状态
// ============================================================================
bool connected = false;           // BLE是否已连接
bool isCapturingPhotos = false;   // 是否正在拍照
int captureInterval = 0;          // 拍照间隔(ms), 0表示单次拍照
unsigned long lastCaptureTime = 0; // 上次拍照时间戳

// ============================================================================
// 音频传输环形缓冲区
// ============================================================================
// 用于存储已编码的Opus音频包,等待BLE传输
#define AUDIO_TX_BUFFER_SIZE (AUDIO_TX_RING_BUFFER_SIZE * (OPUS_OUTPUT_MAX_BYTES + 2))
static uint8_t audio_tx_buffer[AUDIO_TX_BUFFER_SIZE];        // 环形缓冲区
static volatile size_t audio_tx_write_pos = 0;               // 写入位置
static volatile size_t audio_tx_read_pos = 0;                // 读取位置
static uint8_t audio_packet_buffer[OPUS_OUTPUT_MAX_BYTES + AUDIO_PACKET_HEADER_SIZE]; // 打包缓冲区

// ============================================================================
// 照片传输状态
// ============================================================================
size_t sent_photo_bytes = 0;    // 已发送的照片字节数
size_t sent_photo_frames = 0;   // 已发送的照片帧数
bool photoDataUploading = false; // 照片是否正在上传

// ============================================================================
// 相机帧缓冲区
// ============================================================================
camera_fb_t *fb = nullptr;  // 当前相机帧缓冲区指针
image_orientation_t current_photo_orientation = ORIENTATION_0_DEGREES; // 照片旋转角度

// ============================================================================
// 函数前向声明
// ============================================================================

// 照片和电池相关
void handlePhotoControl(int8_t controlValue); // 处理照片控制命令
void readBatteryLevel();                      // 读取电池电量
void updateBatteryService();                  // 更新BLE电池服务

// 按钮和LED相关
void IRAM_ATTR buttonISR();      // 按钮中断服务程序
void handleButton();             // 处理按钮事件(防抖和长按检测)
void updateLED();                // 更新LED状态
void blinkLED(int count, int delayMs); // LED闪烁

// 电源管理相关
void enterPowerSave();   // 进入省电模式(降低CPU频率)
void exitPowerSave();    // 退出省电模式(恢复CPU频率)
void shutdownDevice();   // 关机(进入深度睡眠)
void enableLightSleep(); // 启用轻度睡眠

// 音频相关回调和处理
void onMicData(int16_t *data, size_t samples);       // 麦克风数据回调
void onOpusEncoded(uint8_t *data, size_t len);       // Opus编码完成回调
void processAudioTx();                               // 处理音频发送队列
void broadcastAudioPacket(uint8_t *data, size_t len); // 通过BLE广播音频包

// ============================================================================
// 按钮中断服务程序 (ISR)
// ============================================================================

/**
 * buttonISR - 按钮中断处理程序
 *
 * 当按钮状态变化时触发(按下或释放)
 * 使用IRAM_ATTR确保函数在RAM中执行,提高中断响应速度
 *
 * 功能:简单设置标志位,实际处理在主循环中进行
 */
void IRAM_ATTR buttonISR()
{
    buttonPressed = true;
}

// ============================================================================
// LED控制函数
// ============================================================================

/**
 * updateLED - 更新LED状态
 *
 * 根据当前LED模式控制LED行为:
 * - LED_BOOT_SEQUENCE: 启动时快速闪烁5次(1.5秒)
 * - LED_POWER_OFF_SEQUENCE: 关机时快速闪烁2次(0.8秒)
 * - LED_NORMAL_OPERATION: 正常运行,已连接常亮,未连接慢闪
 *
 * 注意:LED使用反向逻辑(HIGH=关, LOW=开)
 */
void updateLED()
{
    unsigned long now = millis();
    static unsigned long bootStartTime = 0;
    static unsigned long powerOffStartTime = 0;

    switch (ledMode) {
    case LED_BOOT_SEQUENCE:
        if (bootStartTime == 0)
            bootStartTime = now;

        // 启动序列:5次快速闪烁,总时长1.5秒(反向逻辑:HIGH=关,LOW=开)
        if (now - bootStartTime < 1500) {
            int blinkPhase = ((now - bootStartTime) / 150) % 2;
            digitalWrite(STATUS_LED_PIN, !blinkPhase);
        } else {
            digitalWrite(STATUS_LED_PIN, HIGH); // 关闭LED
            ledMode = LED_NORMAL_OPERATION;     // 切换到正常模式
            bootStartTime = 0;
        }
        break;

    case LED_POWER_OFF_SEQUENCE:
        if (powerOffStartTime == 0)
            powerOffStartTime = now;

        // 关机序列:2次快速闪烁,总时长0.8秒
        if (now - powerOffStartTime < 800) {
            int blinkPhase = ((now - powerOffStartTime) / 200) % 2;
            digitalWrite(STATUS_LED_PIN, !blinkPhase);
        } else {
            digitalWrite(STATUS_LED_PIN, HIGH); // 关闭LED
            delay(100);
            shutdownDevice(); // 执行关机
        }
        break;

    case LED_NORMAL_OPERATION:
    default:
        if (connected) {
            // 已连接BLE:LED常亮
            digitalWrite(STATUS_LED_PIN, LOW);
        } else {
            // 未连接BLE:LED慢闪(1秒开,1秒关)
            int blinkPhase = (now / 1000) % 2;
            digitalWrite(STATUS_LED_PIN, blinkPhase ? HIGH : LOW);
        }
        break;
    }
}

/**
 * blinkLED - 手动LED闪烁
 *
 * @param {int} count - 闪烁次数
 * @param {int} delayMs - 每次闪烁的延迟时间(ms)
 *
 * 用于测试或特殊事件指示
 * 注意:这个函数会阻塞执行
 */
void blinkLED(int count, int delayMs)
{
    for (int i = 0; i < count; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH); // 关闭
        delay(delayMs);
        digitalWrite(STATUS_LED_PIN, LOW);  // 开启
        delay(delayMs);
    }
}

// ============================================================================
// 按钮处理函数
// ============================================================================

/**
 * handleButton - 按钮事件处理(防抖和长按检测)
 *
 * 功能说明:
 * - 防抖处理:忽略50ms内的抖动
 * - 长按检测:按下2秒触发关机序列
 * - 短按处理:唤醒设备,退出省电模式
 *
 * 按钮是低电平有效(按下=LOW, 释放=HIGH)
 * 主循环中需要定期调用此函数
 */
void handleButton()
{
    unsigned long now = millis();
    static unsigned long lastDebounceTime = 0;
    static bool buttonDown = false;
    static bool longPressTriggered = false;

    // 读取当前按钮状态(反转,因为是低电平有效)
    bool currentButtonState = !digitalRead(POWER_BUTTON_PIN); // 按下=true

    if (currentButtonState && !buttonDown) {
        // 按钮刚刚按下 - 执行防抖
        if (now - lastDebounceTime < 50) {
            return; // 50ms内忽略
        }
        buttonPressTime = now;
        buttonDown = true;
        longPressTriggered = false;
        lastDebounceTime = now;

    } else if (currentButtonState && buttonDown && !longPressTriggered) {
        // 按钮保持按下状态 - 检查长按
        unsigned long pressDuration = now - buttonPressTime;
        if (pressDuration >= 2000) {
            // 长按阈值(2秒)已到达 - 立即触发关机
            longPressTriggered = true;
            ledMode = LED_POWER_OFF_SEQUENCE; // 启动关机LED序列
        }

    } else if (!currentButtonState && buttonDown) {
        // 按钮刚刚释放 - 防抖
        if (now - lastDebounceTime < 50) {
            return; // 50ms内忽略
        }
        buttonDown = false;
        unsigned long pressDuration = now - buttonPressTime;
        lastDebounceTime = now;

        // 只处理短按(长按已经触发过了)
        if (!longPressTriggered && pressDuration >= 50) {
            // 短按 - 注册活动,唤醒设备
            lastActivity = now;
            if (powerSaveMode) {
                exitPowerSave(); // 退出省电模式
            }
        }
        longPressTriggered = false;
    }

    buttonPressed = false; // 清除ISR标志
}

// ============================================================================
// 电源管理函数
// ============================================================================

/**
 * enterPowerSave - 进入省电模式
 *
 * 降低CPU频率到40MHz以节省功耗
 * 在长时间无活动时自动调用
 */
void enterPowerSave()
{
    if (!powerSaveMode) {
        setCpuFrequencyMhz(MIN_CPU_FREQ_MHZ); // 降频到40MHz
        powerSaveMode = true;
    }
}

/**
 * exitPowerSave - 退出省电模式
 *
 * 恢复CPU频率到80MHz
 * 在检测到活动时自动调用
 */
void exitPowerSave()
{
    if (powerSaveMode) {
        setCpuFrequencyMhz(NORMAL_CPU_FREQ_MHZ); // 恢复到80MHz
        powerSaveMode = false;
    }
}

/**
 * enableLightSleep - 启用轻度睡眠
 *
 * 在以下条件满足时进入轻度睡眠:
 * - 已连接BLE(避免广播中断)
 * - 没有照片上传任务
 * - 最近5秒无活动
 * - 距离下次拍照>10秒
 *
 * 轻度睡眠可节省约15mA电流,增加3-4小时续航
 * 睡眠时会自动唤醒处理BLE事件和定时器
 */
void enableLightSleep()
{
    // 检查是否允许睡眠
    if (!lightSleepEnabled || !connected || photoDataUploading) {
        return; // 未启用/未连接/正在上传时不睡眠
    }

    unsigned long now = millis();

    // 最近5秒有活动则不睡眠
    if (now - lastActivity < 5000) {
        return;
    }

    unsigned long timeUntilNextPhoto = 0;

    // 计算距离下次拍照的时间
    if (isCapturingPhotos && captureInterval > 0) {
        unsigned long timeSinceLastPhoto = now - lastCaptureTime;
        if (timeSinceLastPhoto < captureInterval) {
            timeUntilNextPhoto = captureInterval - timeSinceLastPhoto;
        }
    }

    // 只在距离下次拍照>10秒时睡眠
    if (timeUntilNextPhoto > 10000) {
        // 配置轻度睡眠定时器(在拍照前5秒唤醒,或最多睡15秒)
        unsigned long sleepTime = timeUntilNextPhoto - 5000;
        if (sleepTime > 15000)
            sleepTime = 15000;                           // 最多睡15秒
        esp_sleep_enable_timer_wakeup(sleepTime * 1000); // 设置唤醒定时器(微秒)
        esp_light_sleep_start();                         // 进入轻度睡眠
        lastActivity = millis();                         // 唤醒后更新活动时间
    }
}

/**
 * shutdownDevice - 关闭设备
 *
 * 执行完整的关机流程:
 * 1. 停止音频采集
 * 2. 停止照片拍摄
 * 3. 断开BLE连接
 * 4. 关闭LED
 * 5. 进入深度睡眠
 *
 * 深度睡眠可通过按下电源按钮唤醒(GPIO1)
 */
void shutdownDevice()
{
    Serial.println("Shutting down device...");

    // 停止音频子系统
    mic_stop();

    // 停止照片拍摄
    isCapturingPhotos = false;

    // 断开BLE连接(如果已连接)
    if (connected) {
        Serial.println("Disconnecting BLE...");
    }

    // 关闭LED(反向逻辑)
    digitalWrite(STATUS_LED_PIN, HIGH);

    // 配置深度睡眠唤醒源:电源按钮(GPIO1)按下时唤醒
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_1, 0); // 低电平唤醒
    Serial.println("Entering deep sleep...");
    delay(100);
    esp_deep_sleep_start(); // 进入深度睡眠(永久,直到按钮唤醒)
}


// ============================================================================
// 音频处理函数
// ============================================================================

/**
 * onMicData - 麦克风数据回调函数
 *
 * @param {int16_t*} data - PCM音频数据(16位有符号整数)
 * @param {size_t} samples - 采样点数量
 *
 * 当麦克风模块读取到新数据时调用此函数
 * 将PCM数据传递给Opus编码器进行压缩
 */
void onMicData(int16_t *data, size_t samples)
{
    // 将PCM数据传递给Opus编码器
    opus_receive_pcm(data, samples);
}

/**
 * onOpusEncoded - Opus编码完成回调函数
 *
 * @param {uint8_t*} data - 编码后的Opus数据
 * @param {size_t} len - 数据长度(字节)
 *
 * 当Opus编码器完成一帧编码时调用
 * 将编码数据存入发送环形缓冲区,等待BLE传输
 *
 * 环形缓冲区格式: [长度(2字节), 数据(len字节)]
 */
void onOpusEncoded(uint8_t *data, size_t len)
{
    // 检查数据长度是否有效
    if (len > OPUS_OUTPUT_MAX_BYTES) {
        return; // 数据过大,丢弃
    }

    // 计算包大小: 长度字段(2字节) + 数据
    size_t packet_size = len + 2;
    size_t next_write = (audio_tx_write_pos + packet_size) % AUDIO_TX_BUFFER_SIZE;

    // 检查环形缓冲区是否会溢出
    if ((audio_tx_write_pos < audio_tx_read_pos && next_write >= audio_tx_read_pos) ||
        (audio_tx_write_pos >= audio_tx_read_pos && next_write < audio_tx_write_pos &&
         next_write >= audio_tx_read_pos)) {
        // 缓冲区满,丢弃此包(实时音频可以容忍丢包)
        return;
    }

    // 写入数据长度(小端格式,2字节)
    audio_tx_buffer[audio_tx_write_pos] = len & 0xFF;
    audio_tx_buffer[(audio_tx_write_pos + 1) % AUDIO_TX_BUFFER_SIZE] = (len >> 8) & 0xFF;

    // 写入编码数据
    for (size_t i = 0; i < len; i++) {
        audio_tx_buffer[(audio_tx_write_pos + 2 + i) % AUDIO_TX_BUFFER_SIZE] = data[i];
    }

    // 更新写入位置
    audio_tx_write_pos = next_write;
}

/**
 * broadcastAudioPacket - 通过BLE广播音频包
 *
 * @param {uint8_t*} data - Opus编码数据
 * @param {size_t} len - 数据长度
 *
 * 将音频数据打包并通过BLE通知发送给客户端
 * 包格式: [包序号(2字节), 子序号(1字节), Opus数据]
 */
void broadcastAudioPacket(uint8_t *data, size_t len)
{
    // 检查连接状态和订阅状态
    if (!connected || !audioSubscribed || audioDataCharacteristic == nullptr) {
        return; // 未连接或未订阅,不发送
    }

    // 构建音频包: 包序号(2字节) + 子序号(1字节) + 数据
    audio_packet_buffer[0] = audioPacketIndex & 0xFF;        // 包序号低字节
    audio_packet_buffer[1] = (audioPacketIndex >> 8) & 0xFF; // 包序号高字节
    audio_packet_buffer[2] = 0;                              // 子序号(用于分片,当前未使用)

    // 复制音频数据
    memcpy(audio_packet_buffer + AUDIO_PACKET_HEADER_SIZE, data, len);

    // 发送BLE通知
    audioDataCharacteristic->setValue(audio_packet_buffer, len + AUDIO_PACKET_HEADER_SIZE);
    audioDataCharacteristic->notify();

    // 递增包序号
    audioPacketIndex++;
}

/**
 * processAudioTx - 处理音频发送队列
 *
 * 从发送环形缓冲区读取已编码的音频包,并通过BLE发送
 * 应在主循环中定期调用,优先级高于照片传输
 *
 * 功能说明:
 * 1. 从环形缓冲区读取长度字段
 * 2. 读取对应长度的数据
 * 3. 调用broadcastAudioPacket发送
 * 4. 每次发送后延迟1ms防止BLE拥塞
 */
void processAudioTx()
{
    // 检查连接和订阅状态
    if (!connected || !audioSubscribed) {
        return;
    }

    if (audioDataCharacteristic == nullptr) {
        return;
    }

    // 循环处理环形缓冲区中的所有数据
    while (audio_tx_read_pos != audio_tx_write_pos) {
        // 读取数据长度(小端格式,2字节)
        uint16_t len =
            audio_tx_buffer[audio_tx_read_pos] | (audio_tx_buffer[(audio_tx_read_pos + 1) % AUDIO_TX_BUFFER_SIZE] << 8);

        // 检查长度是否有效
        if (len == 0 || len > OPUS_OUTPUT_MAX_BYTES) {
            // 无效包,跳过长度字段
            audio_tx_read_pos = (audio_tx_read_pos + 2) % AUDIO_TX_BUFFER_SIZE;
            continue;
        }

        // 读取数据到临时缓冲区
        static uint8_t temp_data[OPUS_OUTPUT_MAX_BYTES];
        for (size_t i = 0; i < len; i++) {
            temp_data[i] = audio_tx_buffer[(audio_tx_read_pos + 2 + i) % AUDIO_TX_BUFFER_SIZE];
        }

        // 更新读取位置
        audio_tx_read_pos = (audio_tx_read_pos + 2 + len) % AUDIO_TX_BUFFER_SIZE;

        // 发送音频包
        broadcastAudioPacket(temp_data, len);

        // 小延迟防止BLE拥塞
        delay(1);
    }
}

// ============================================================================
// BLE服务器回调类
// ============================================================================

/**
 * ServerHandler - BLE服务器事件处理器
 *
 * 处理BLE连接和断开事件
 */
class ServerHandler : public BLEServerCallbacks
{
    /**
     * onConnect - 客户端连接事件
     *
     * 当有客户端连接时调用
     * 重置订阅状态并发送当前电池电量
     */
    void onConnect(BLEServer *server) override
    {
        connected = true;
        audioSubscribed = false;
        lastActivity = millis(); // 注册活动,防止睡眠
        Serial.println(">>> BLE Client connected.");
        // 连接时发送当前电池电量
        updateBatteryService();
    }

    /**
     * onDisconnect - 客户端断开事件
     *
     * 当客户端断开连接时调用
     * 重新开始广播以便重新连接
     */
    void onDisconnect(BLEServer *server) override
    {
        connected = false;
        audioSubscribed = false;
        Serial.println("<<< BLE Client disconnected. Restarting advertising.");
        BLEDevice::startAdvertising(); // 重新开始广播
    }
};

/**
 * AudioCCCDCallback - 音频订阅状态回调
 *
 * 处理客户端对音频通知的订阅/取消订阅
 * CCCD = Client Characteristic Configuration Descriptor
 */
class AudioCCCDCallback : public BLEDescriptorCallbacks
{
    /**
     * onWrite - 客户端写入CCCD时调用
     *
     * 检查客户端是否启用了通知(bit 0)
     */
    void onWrite(BLEDescriptor *pDescriptor)
    {
        uint8_t *value = pDescriptor->getValue();
        if (value && pDescriptor->getLength() >= 2) {
            // 检查通知位(bit 0)
            if (value[0] & 0x01) {
                audioSubscribed = true;
                Serial.println("Audio notifications enabled");
            } else {
                audioSubscribed = false;
                Serial.println("Audio notifications disabled");
            }
        }
    }
};

/**
 * AudioDataCallback - 音频数据特性回调
 *
 * 处理音频特性的读写和状态事件
 */
class AudioDataCallback : public BLECharacteristicCallbacks
{
    /**
     * onStatus - 通知发送状态回调
     *
     * 当通知发送成功时调用
     */
    void onStatus(BLECharacteristic *pCharacteristic, Status s, uint32_t code)
    {
        if (s == Status::SUCCESS_NOTIFY || s == Status::SUCCESS_INDICATE) {
            // 通知发送成功(可在此处添加统计逻辑)
        }
    }

    /**
     * onRead - 客户端读取特性时调用
     *
     * 当前未使用(音频是单向推送)
     */
    void onRead(BLECharacteristic *pCharacteristic)
    {
        // 客户端读取特性(当前未使用,音频是单向推送)
    }
};

/**
 * PhotoControlCallback - 照片控制特性回调
 *
 * 处理客户端发送的照片控制命令
 * 命令格式:单字节有符号整数
 *   -1: 拍摄单张照片
 *    0: 停止拍照
 *  5-300: 设置间隔拍照(秒数)
 */
class PhotoControlCallback : public BLECharacteristicCallbacks
{
    /**
     * onWrite - 客户端写入控制命令时调用
     *
     * 解析命令并调用handlePhotoControl处理
     */
    void onWrite(BLECharacteristic *characteristic) override
    {
        if (characteristic->getLength() == 1) {
            int8_t received = characteristic->getData()[0];
            Serial.print("PhotoControl received: ");
            Serial.println(received);
            lastActivity = millis(); // 注册活动,防止睡眠
            handlePhotoControl(received); // 处理照片控制命令
        }
    }
};

/**
 * OTAControlCallback - OTA控制特性回调
 *
 * 处理固件OTA升级相关的命令
 * 支持WiFi配置、固件下载、升级进度查询等
 */
class OTAControlCallback : public BLECharacteristicCallbacks
{
    /**
     * onWrite - 客户端写入OTA命令时调用
     *
     * 命令格式由ota模块定义:
     * - 0x01: 设置WiFi凭据
     * - 0x02: 开始OTA升级
     * - 0x03: 取消OTA
     * - 0x04: 查询状态
     * - 0x05: 设置固件URL
     */
    void onWrite(BLECharacteristic *pChar) override
    {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            ota_handle_command((uint8_t *) value.data(), value.length());
        }
    }

    /**
     * onRead - 客户端读取OTA状态时调用
     *
     * 返回当前OTA状态码(2字节)
     */
    void onRead(BLECharacteristic *pChar) override
    {
        uint8_t status[2] = {ota_get_status(), 0};
        pChar->setValue(status, 2);
    }
};

// ============================================================================
// 电池管理函数
// ============================================================================

/**
 * readBatteryLevel - 读取电池电量
 *
 * 功能说明:
 * 1. 通过ADC读取电池电压(GPIO2)
 * 2. 采样10次取平均,提高稳定性
 * 3. 使用电压分压器计算实际电池电压
 * 4. 考虑负载补偿,计算电量百分比
 * 5. 平滑处理,避免电量跳变
 *
 * 电压范围:
 * - 最大电压: 4.2V (满电)
 * - 最小电压: 3.2V (空电)
 * - 临界电压: 3.3V (紧急关机)
 *
 * 技术要点:
 * - 使用分压器读取双电池总电压(500mAh总容量)
 * - 负载补偿:考虑负载下的电压降
 * - 平滑算法:变化>5%时渐进调整(±2%/次)
 */
void readBatteryLevel()
{
    // 采样10次取平均值,提高稳定性
    int adcSum = 0;
    for (int i = 0; i < 10; i++) {
        int value = analogRead(BATTERY_ADC_PIN); // GPIO2/A1
        adcSum += value;
        delay(10); // 每次采样间隔10ms
    }
    int adcValue = adcSum / 10;

    // ESP32-S3 ADC: 12位(0-4095), 参考电压约3.3V
    float adcVoltage = (adcValue / 4095.0f) * 3.3f;

    // 应用分压器比例计算实际电池电压
    batteryVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO; // 6.086倍

    // 限制电压到合理范围(防止异常读数)
    if (batteryVoltage > 5.0f)
        batteryVoltage = 5.0f;
    if (batteryVoltage < 2.5f)
        batteryVoltage = 2.5f;

    // 负载补偿电池计算(考虑负载下的电压降)
    float loadCompensatedMax = BATTERY_MAX_VOLTAGE; // 4.2V
    float loadCompensatedMin = BATTERY_MIN_VOLTAGE; // 3.2V

    // 根据电压计算电量百分比(负载条件下的精确计算)
    if (batteryVoltage >= loadCompensatedMax) {
        batteryPercentage = 100; // 满电
    } else if (batteryVoltage <= loadCompensatedMin) {
        batteryPercentage = 0;   // 空电
    } else {
        // 线性映射电压到百分比
        float range = loadCompensatedMax - loadCompensatedMin;
        batteryPercentage = (int) (((batteryVoltage - loadCompensatedMin) / range) * 100.0f);
    }

    // 平滑百分比变化,避免跳变(用户体验优化)
    static int lastBatteryPercentage = batteryPercentage;
    if (abs(batteryPercentage - lastBatteryPercentage) > 5) {
        // 变化>5%时,每次只调整2%,渐进变化
        batteryPercentage = lastBatteryPercentage + (batteryPercentage > lastBatteryPercentage ? 2 : -2);
    }
    lastBatteryPercentage = batteryPercentage;

    // 限制百分比到0-100范围
    if (batteryPercentage > 100)
        batteryPercentage = 100;
    if (batteryPercentage < 0)
        batteryPercentage = 0;

    // 打印电池状态(包含负载补偿信息)
    Serial.print("Battery: ");
    Serial.print(batteryVoltage);
    Serial.print("V (");
    Serial.print(batteryPercentage);
    Serial.print("%) [Load-compensated: ");
    Serial.print(loadCompensatedMin);
    Serial.print("V-");
    Serial.print(loadCompensatedMax);
    Serial.println("V]");
}

/**
 * updateBatteryService - 更新BLE电池服务
 *
 * 将当前电池电量通过BLE通知发送给客户端
 * 使用标准电池服务(UUID 0x180F)
 */
void updateBatteryService()
{
    if (batteryLevelCharacteristic) {
        uint8_t batteryLevel = (uint8_t) batteryPercentage;
        batteryLevelCharacteristic->setValue(&batteryLevel, 1);

        // 如果已连接,发送通知给客户端
        if (connected) {
            batteryLevelCharacteristic->notify();
        }
    }
}

// ============================================================================
// BLE服务配置函数
// ============================================================================

/**
 * configure_ble - 初始化和配置BLE服务
 *
 * 创建并配置所有BLE服务和特性:
 * 1. OMI主服务(音频、照片)
 * 2. 标准电池服务(0x180F)
 * 3. 标准设备信息服务(0x180A)
 * 4. OTA升级服务
 *
 * 设置回调函数并开始广播
 *
 * BLE架构:
 * - 设备名称: "OMI Glass"
 * - MTU: 517字节
 * - 广播间隔: 200-400ms(省电优化)
 * - 传输功率: 0dBm(低功耗)
 */
void configure_ble()
{
    Serial.println("Initializing BLE...");
    // 初始化BLE设备
    BLEDevice::init(BLE_DEVICE_NAME); // "OMI Glass"
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerHandler()); // 设置连接/断开回调

    // ========================================================================
    // OMI主服务 - 照片和音频传输
    // ========================================================================
    BLEService *service = server->createService(serviceUUID);

    // 音频数据特性(用于流式传输音频到应用)
    audioDataCharacteristic = service->createCharacteristic(
        audioDataUUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLE2902 *audioCcc = new BLE2902(); // CCCD描述符(客户端配置描述符)
    audioCcc->setNotifications(true);
    audioCcc->setCallbacks(new AudioCCCDCallback()); // 监听订阅状态变化
    audioDataCharacteristic->addDescriptor(audioCcc);
    audioDataCharacteristic->setCallbacks(new AudioDataCallback());

    // 音频编解码器特性(告知应用使用的编解码器)
    audioCodecCharacteristic = service->createCharacteristic(audioCodecUUID, BLECharacteristic::PROPERTY_READ);
    uint8_t codecId = opus_get_codec_id(); // 获取Opus编解码器ID(21)
    audioCodecCharacteristic->setValue(&codecId, 1);

    // 照片数据特性(用于传输JPEG照片)
    photoDataCharacteristic = service->createCharacteristic(
        photoDataUUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLE2902 *ccc = new BLE2902();
    ccc->setNotifications(true);
    photoDataCharacteristic->addDescriptor(ccc);

    // 照片控制特性(接收拍照命令)
    photoControlCharacteristic = service->createCharacteristic(photoControlUUID, BLECharacteristic::PROPERTY_WRITE);
    photoControlCharacteristic->setCallbacks(new PhotoControlCallback());
    uint8_t controlValue = 0;
    photoControlCharacteristic->setValue(&controlValue, 1);

    // ========================================================================
    // 标准电池服务(Bluetooth SIG定义)
    // ========================================================================
    BLEService *batteryService = server->createService(BATTERY_SERVICE_UUID); // 0x180F
    batteryLevelCharacteristic = batteryService->createCharacteristic(
        BATTERY_LEVEL_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY); // 0x2A19
    BLE2902 *batteryCcc = new BLE2902();
    batteryCcc->setNotifications(true);
    batteryLevelCharacteristic->addDescriptor(batteryCcc);

    // 设置初始电池电量
    readBatteryLevel();
    uint8_t initialBatteryLevel = (uint8_t) batteryPercentage;
    batteryLevelCharacteristic->setValue(&initialBatteryLevel, 1);

    // ========================================================================
    // 标准设备信息服务(Bluetooth SIG定义)
    // ========================================================================
    BLEService *deviceInfoService = server->createService(DEVICE_INFORMATION_SERVICE_UUID); // 0x180A

    // 制造商名称
    BLECharacteristic *manufacturerNameCharacteristic =
        deviceInfoService->createCharacteristic(MANUFACTURER_NAME_STRING_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    // 型号
    BLECharacteristic *modelNumberCharacteristic =
        deviceInfoService->createCharacteristic(MODEL_NUMBER_STRING_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    // 固件版本
    BLECharacteristic *firmwareRevisionCharacteristic =
        deviceInfoService->createCharacteristic(FIRMWARE_REVISION_STRING_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    // 硬件版本
    BLECharacteristic *hardwareRevisionCharacteristic =
        deviceInfoService->createCharacteristic(HARDWARE_REVISION_STRING_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    // 序列号
    BLECharacteristic *serialNumberCharacteristic =
        deviceInfoService->createCharacteristic(SERIAL_NUMBER_STRING_CHAR_UUID, BLECharacteristic::PROPERTY_READ);

    // 设置设备信息值(从config.h获取)
    manufacturerNameCharacteristic->setValue(MANUFACTURER_NAME);       // "Based Hardware"
    modelNumberCharacteristic->setValue(BLE_DEVICE_NAME);              // "OMI Glass"
    firmwareRevisionCharacteristic->setValue(FIRMWARE_VERSION_STRING); // "2.3.2"
    hardwareRevisionCharacteristic->setValue(HARDWARE_REVISION);       // "ESP32-S3-v1.0"

    // 从ESP32芯片ID生成唯一序列号
    uint64_t chipId = ESP.getEfuseMac(); // 获取芯片唯一ID
    char serialNumber[17];
    snprintf(serialNumber, sizeof(serialNumber), "%04X%08X", (uint16_t) (chipId >> 32), (uint32_t) chipId);
    serialNumberCharacteristic->setValue(serialNumber);

    // ========================================================================
    // OTA升级服务
    // ========================================================================
    BLEService *otaService = server->createService(otaServiceUUID);

    // OTA控制特性(用于接收命令和读取状态)
    otaControlCharacteristic = otaService->createCharacteristic(
        otaControlUUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    otaControlCharacteristic->setCallbacks(new OTAControlCallback());

    // OTA数据特性(用于进度通知)
    otaDataCharacteristic = otaService->createCharacteristic(
        otaDataUUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLE2902 *otaCcc = new BLE2902();
    otaCcc->setNotifications(true);
    otaDataCharacteristic->addDescriptor(otaCcc);

    // 将OTA特性传递给OTA模块
    ota_set_characteristics(otaControlCharacteristic, otaDataCharacteristic);

    // ========================================================================
    // 启动所有服务
    // ========================================================================
    service->start();         // OMI主服务
    batteryService->start();  // 电池服务
    deviceInfoService->start(); // 设备信息服务
    otaService->start();      // OTA服务

    // ========================================================================
    // 开始BLE广播
    // ========================================================================
    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(service->getUUID()); // 广播OMI服务UUID(适配31字节限制)
    advertising->setScanResponse(true);              // 启用扫描响应
    advertising->setMinPreferred(BLE_ADV_MIN_INTERVAL); // 最小广播间隔200ms
    advertising->setMaxPreferred(BLE_ADV_MAX_INTERVAL); // 最大广播间隔400ms
    BLEDevice::startAdvertising();

    Serial.println("BLE initialized and advertising started.");
}

// ============================================================================
// 相机函数
// ============================================================================

/**
 * take_photo - 拍摄照片
 *
 * @returns {bool} 成功返回true,失败返回false
 *
 * 功能说明:
 * 1. 释放之前的帧缓冲区(如果有)
 * 2. 从相机获取新的JPEG图像
 * 3. 设置照片旋转角度(固定180度)
 * 4. 注册活动时间戳
 *
 * 照片参数(从config.h):
 * - 分辨率: VGA 640x480
 * - JPEG质量: 25
 * - 平均大小: ~10KB
 * - 旋转角度: 180度(因为相机安装是倒置的)
 */
bool take_photo()
{
    // 释放之前的帧缓冲区(防止内存泄漏)
    if (fb) {
        Serial.println("Releasing previous camera buffer...");
        esp_camera_fb_return(fb);
        fb = nullptr;
    }

    Serial.println("Capturing photo...");
    // 从相机获取帧缓冲区(硬件JPEG编码)
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Failed to get camera frame buffer!");
        return false;
    }
    Serial.print("Photo captured: ");
    Serial.print(fb->len);
    Serial.println(" bytes.");

    // 设置固定的照片旋转角度(180度)
    current_photo_orientation = FIXED_IMAGE_ORIENTATION;
    Serial.println("Photo orientation set to 180 degrees (fixed).");

    lastActivity = millis(); // 注册活动
    return true;
}

/**
 * handlePhotoControl - 处理照片控制命令
 *
 * @param {int8_t} controlValue - 控制命令值
 *   -1: 拍摄单张照片
 *    0: 停止拍照
 *  5-300: 设置间隔拍照(秒数,但会使用固定间隔30秒)
 *
 * 功能说明:
 * - 单张模式:拍摄一张后自动停止
 * - 间隔模式:使用固定的30秒间隔(config.h中定义)
 * - 停止模式:停止所有拍照活动
 *
 * 注意:为了优化电池寿命,间隔时间固定为30秒,
 *       不使用客户端发送的间隔参数
 */
void handlePhotoControl(int8_t controlValue)
{
    if (controlValue == -1) {
        // 单张拍照模式
        Serial.println("Received command: Single photo.");
        isCapturingPhotos = true;
        captureInterval = 0; // 0表示单次拍照
    } else if (controlValue == 0) {
        // 停止拍照
        Serial.println("Received command: Stop photo capture.");
        isCapturingPhotos = false;
        captureInterval = 0;
    } else if (controlValue >= 5 && controlValue <= 300) {
        // 间隔拍照模式
        Serial.print("Received command: Start interval capture with parameter ");
        Serial.println(controlValue);

        // 使用配置文件中的固定间隔以优化电池寿命
        captureInterval = PHOTO_CAPTURE_INTERVAL_MS; // 30秒(30000ms)
        Serial.print("Using configured interval: ");
        Serial.print(captureInterval / 1000);
        Serial.println(" seconds");

        isCapturingPhotos = true;
        lastCaptureTime = millis() - captureInterval; // 立即触发第一次拍照
    }
}

// ============================================================================
// 相机配置函数
// ============================================================================

/**
 * configure_camera - 初始化相机
 *
 * 配置OV2640相机模块,使用以下参数:
 * - 分辨率: VGA 640x480
 * - 格式: JPEG
 * - 质量: 25
 * - 时钟频率: 6MHz(省电优化)
 * - 帧缓冲: PSRAM(节省内部RAM)
 *
 * 相机引脚配置从camera_pins.h获取
 */
// -------------------------------------------------------------------------
void configure_camera()
{
    Serial.println("Initializing camera...");

    // 配置相机参数结构体
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    // 数据引脚配置(从camera_pins.h获取)
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    // 控制引脚配置
    config.pin_xclk = XCLK_GPIO_NUM;   // 时钟引脚
    config.pin_pclk = PCLK_GPIO_NUM;   // 像素时钟
    config.pin_vsync = VSYNC_GPIO_NUM; // 垂直同步
    config.pin_href = HREF_GPIO_NUM;   // 水平参考
    config.pin_sscb_sda = SIOD_GPIO_NUM; // I2C数据(SCCB)
    config.pin_sscb_scl = SIOC_GPIO_NUM; // I2C时钟(SCCB)
    config.pin_pwdn = PWDN_GPIO_NUM;   // 电源控制
    config.pin_reset = RESET_GPIO_NUM; // 复位引脚
    config.xclk_freq_hz = CAMERA_XCLK_FREQ; // 时钟频率6MHz(省电)

    // 使用config.h中优化的相机设置(针对电池寿命优化)
    config.frame_size = CAMERA_FRAME_SIZE;     // VGA 640x480
    config.pixel_format = PIXFORMAT_JPEG;      // JPEG格式
    config.fb_count = 1;                       // 单帧缓冲
    config.jpeg_quality = CAMERA_JPEG_QUALITY; // JPEG质量25
    config.fb_location = CAMERA_FB_IN_PSRAM;   // 帧缓冲在PSRAM
    config.grab_mode = CAMERA_GRAB_LATEST;     // 获取最新帧

    // 初始化相机
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
    } else {
        Serial.println("Camera initialized successfully.");
    }
}

// ============================================================================
// 主初始化和循环函数
// ============================================================================

// 照片分块传输缓冲区(全局静态变量)
static uint8_t *s_compressed_frame_2 = nullptr;

/**
 * setup_app - 应用初始化函数
 *
 * 在系统启动时调用一次,初始化所有硬件和软件模块:
 * 1. 串口通信(921600波特率)
 * 2. GPIO配置(按钮、LED)
 * 3. 按钮中断
 * 4. CPU频率设置(80MHz)
 * 5. BLE服务
 * 6. 相机模块
 * 7. 照片传输缓冲区
 * 8. 电池监控ADC
 * 9. 音频子系统(麦克风+Opus编码)
 *
 * 初始化完成后,设备进入正常运行状态
 */
void setup_app()
{
    Serial.begin(921600);
    Serial.println("Setup started...");

    // ========================================================================
    // GPIO初始化
    // ========================================================================
    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP); // 电源按钮(GPIO1,上拉)
    pinMode(STATUS_LED_PIN, OUTPUT);         // 状态LED(GPIO21)

    // LED使用反向逻辑: HIGH=关, LOW=开
    digitalWrite(STATUS_LED_PIN, HIGH);

    // 设置按钮中断(上升沿和下降沿都触发)
    attachInterrupt(digitalPinToInterrupt(POWER_BUTTON_PIN), buttonISR, CHANGE);

    // 启动LED启动序列(快速闪烁5次)
    ledMode = LED_BOOT_SEQUENCE;

    // ========================================================================
    // 电源优化配置
    // ========================================================================
    setCpuFrequencyMhz(NORMAL_CPU_FREQ_MHZ); // 设置CPU频率为80MHz
    lastActivity = millis();

    // ========================================================================
    // 初始化BLE和相机
    // ========================================================================
    configure_ble();    // 配置BLE服务和特性
    configure_camera(); // 配置相机模块

    // ========================================================================
    // 分配照片传输缓冲区
    // ========================================================================
    // 200字节数据 + 2字节帧序号 = 202字节
    s_compressed_frame_2 = (uint8_t *) ps_calloc(202, sizeof(uint8_t));
    if (!s_compressed_frame_2) {
        Serial.println("Failed to allocate chunk buffer!");
    } else {
        Serial.println("Chunk buffer allocated successfully.");
    }

    // ========================================================================
    // 设置默认拍照间隔
    // ========================================================================
    isCapturingPhotos = true;
    captureInterval = PHOTO_CAPTURE_INTERVAL_MS; // 30秒
    lastCaptureTime = millis() - captureInterval; // 立即触发第一次拍照
    Serial.print("Default capture interval set to ");
    Serial.print(PHOTO_CAPTURE_INTERVAL_MS / 1000);
    Serial.println(" seconds.");

    // ========================================================================
    // 配置电池电压分压器ADC
    // ========================================================================
    analogReadResolution(12);                           // 12位ADC分辨率(0-4095)
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db); // 11dB衰减(0-3.3V范围)

    // 初始电池读数
    readBatteryLevel();
    deviceState = DEVICE_ACTIVE; // 设备进入活跃状态

    // ========================================================================
    // 初始化音频子系统
    // ========================================================================
    Serial.println("Initializing audio subsystem...");
    if (opus_encoder_init()) {
        // 设置Opus编码完成回调
        opus_set_callback(onOpusEncoded);

        // 启动麦克风
        if (mic_start()) {
            // 设置麦克风数据回调
            mic_set_callback(onMicData);
            Serial.println("Audio subsystem initialized successfully.");
        } else {
            Serial.println("Failed to start microphone!");
        }
    } else {
        Serial.println("Failed to initialize Opus encoder!");
    }

    Serial.println("Setup complete.");
    Serial.println("Light sleep optimization enabled for extended battery life.");
}

/**
 * loop_app - 应用主循环
 *
 * 在setup_app()完成后持续循环执行
 * 任务优先级(从高到低):
 * 1. 按钮处理(用户交互)
 * 2. LED更新(视觉反馈)
 * 3. OTA升级(安全优先)
 * 4. 音频处理(实时性要求高)
 * 5. 音频传输(优先于照片)
 * 6. 电源管理(省电优化)
 * 7. 电池监控(每20秒)
 * 8. 照片拍摄(每30秒)
 * 9. 照片传输(分块,与音频交错)
 * 10. 轻度睡眠(空闲时)
 *
 * 电源管理策略:
 * - 活跃时: 80MHz CPU
 * - 空闲45秒: 40MHz CPU
 * - 空闲>5秒且无任务: 轻度睡眠(节省15mA)
 */
void loop_app()
{
    unsigned long now = millis();

    // ========================================================================
    // 1. 按钮处理(最高优先级)
    // ========================================================================
    handleButton();

    // ========================================================================
    // 2. LED状态更新
    // ========================================================================
    updateLED();

    // ========================================================================
    // 3. OTA升级处理
    // ========================================================================
    ota_loop();

    // ========================================================================
    // 4. 音频数据处理(实时性要求高,总是运行)
    // ========================================================================
    if (audioEnabled && mic_is_running()) {
        mic_process();   // 从I2S读取PCM数据
        opus_process();  // Opus编码
    }

    // ========================================================================
    // 5. 音频传输(优先于照片传输)
    // ========================================================================
    if (connected && audioSubscribed) {
        processAudioTx(); // 发送音频包
    }

    // ========================================================================
    // 6. 电源管理(省电模式切换)
    // ========================================================================
    // 未连接且45秒无活动: 进入省电模式(40MHz)
    if (!connected && !photoDataUploading && (now - lastActivity > IDLE_THRESHOLD_MS)) {
        enterPowerSave();
    }
    // 已连接或正在上传: 退出省电模式(80MHz)
    else if (connected || photoDataUploading) {
        if (powerSaveMode)
            exitPowerSave();
        lastActivity = now;
    }

    // ========================================================================
    // 7. 电池电量监控(每20秒检查一次)
    // ========================================================================
    if (now - lastBatteryCheck >= BATTERY_TASK_INTERVAL_MS) {
        readBatteryLevel();
        updateBatteryService(); // 通过BLE通知客户端
        lastBatteryCheck = now;
    }

    // 首次连接时强制更新电池电量
    static bool firstBatteryUpdate = true;
    if (connected && firstBatteryUpdate) {
        readBatteryLevel();
        updateBatteryService();
        firstBatteryUpdate = false;
    }

    // ========================================================================
    // 8. 照片拍摄检查(间隔触发)
    // ========================================================================
    if (isCapturingPhotos && !photoDataUploading && connected) {
        // 检查是否到达拍照间隔
        if ((captureInterval == 0) || (now - lastCaptureTime >= (unsigned long) captureInterval)) {
            if (captureInterval == 0) {
                // 单次拍照模式: 拍完后停止
                isCapturingPhotos = false;
            }
            Serial.println("Interval reached. Capturing photo...");
            if (take_photo()) {
                Serial.println("Photo capture successful. Starting upload...");
                photoDataUploading = true;
                sent_photo_bytes = 0;
                sent_photo_frames = 0;
                lastCaptureTime = now;
            }
        }
    }

    // ========================================================================
    // 9. 照片分块传输(与音频交错,每次循环最多发2块)
    // ========================================================================
    static int photo_chunks_this_loop = 0;
    if (photoDataUploading && fb && photo_chunks_this_loop < 2) {
        // 如果音频缓冲区有数据,优先发送音频
        if (audioSubscribed && audio_tx_read_pos != audio_tx_write_pos) {
            photo_chunks_this_loop = 0; // 本次循环重置,下次继续
        } else {
            photo_chunks_this_loop++; // 计数本次循环发送的块数
        }

        size_t remaining = fb->len - sent_photo_bytes;
        if (remaining > 0) {
            size_t bytes_to_copy;

            if (sent_photo_frames == 0) {
                // 第一块: 包含旋转元数据(3字节头)
                s_compressed_frame_2[0] = 0; // 帧序号低字节(固定为0)
                s_compressed_frame_2[1] = 0; // 帧序号高字节(固定为0)
                s_compressed_frame_2[2] = (uint8_t) current_photo_orientation; // 旋转角度
                bytes_to_copy = (remaining > 199) ? 199 : remaining; // 数据最多199字节
                memcpy(&s_compressed_frame_2[3], &fb->buf[sent_photo_bytes], bytes_to_copy);
                photoDataCharacteristic->setValue(s_compressed_frame_2, bytes_to_copy + 3);
            } else {
                // 后续块: 不包含元数据(2字节头)
                s_compressed_frame_2[0] = (uint8_t) (sent_photo_frames & 0xFF);        // 帧序号低字节
                s_compressed_frame_2[1] = (uint8_t) ((sent_photo_frames >> 8) & 0xFF); // 帧序号高字节
                bytes_to_copy = (remaining > 200) ? 200 : remaining; // 数据最多200字节
                memcpy(&s_compressed_frame_2[2], &fb->buf[sent_photo_bytes], bytes_to_copy);
                photoDataCharacteristic->setValue(s_compressed_frame_2, bytes_to_copy + 2);
            }
            photoDataCharacteristic->notify(); // 发送BLE通知

            sent_photo_bytes += bytes_to_copy;
            sent_photo_frames++;

            Serial.print("Uploading chunk ");
            Serial.print(sent_photo_frames);
            Serial.print(" (");
            Serial.print(bytes_to_copy);
            Serial.print(" bytes), ");
            Serial.print(remaining - bytes_to_copy);
            Serial.println(" bytes remaining.");

            lastActivity = now; // 注册活动
        } else {
            // 照片传输完成: 发送结束标记(0xFF 0xFF)
            s_compressed_frame_2[0] = 0xFF;
            s_compressed_frame_2[1] = 0xFF;
            photoDataCharacteristic->setValue(s_compressed_frame_2, 2);
            photoDataCharacteristic->notify();
            Serial.println("Photo upload complete.");

            photoDataUploading = false;
            // 释放相机帧缓冲区
            esp_camera_fb_return(fb);
            fb = nullptr;
            Serial.println("Camera frame buffer freed.");
            photo_chunks_this_loop = 0; // 重置计数器
        }
    } else {
        photo_chunks_this_loop = 0; // 未上传时重置
    }

    // ========================================================================
    // 10. 轻度睡眠优化(空闲时节省功耗)
    // ========================================================================
    // 当没有照片上传且没有音频订阅时启用轻度睡眠
    if (!photoDataUploading && !audioSubscribed) {
        enableLightSleep();
    }

    // ========================================================================
    // 循环延迟(根据任务状态自适应)
    // ========================================================================
    if (photoDataUploading || audioSubscribed) {
        delay(5); // 上传或音频活跃时: 快速循环(5ms)
    } else if (powerSaveMode) {
        delay(50); // 省电模式: 减少循环频率(50ms)
    } else {
        delay(50); // 正常模式: 减少循环频率(50ms)
    }
}
