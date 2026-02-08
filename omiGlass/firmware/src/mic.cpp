/**
 * 麦克风模块 - PDM麦克风I2S接口
 *
 * 负责从XIAO ESP32S3 Sense板载PDM麦克风采集音频数据
 * 主要功能:
 * 1. 初始化I2S PDM麦克风
 * 2. 读取音频数据并应用增益
 * 3. 通过回调函数将音频数据传递给编码器
 */
#include "mic.h"

#include <driver/i2s.h>

#include "config.h"

// I2S端口配置:使用I2S_NUM_0端口
#define I2S_PORT I2S_NUM_0

// 静态变量
static volatile bool mic_running = false;          // 麦克风运行状态标志
static mic_data_handler audio_callback = nullptr;  // 音频数据回调函数
static int16_t *i2s_read_buffer = nullptr;         // I2S读取缓冲区

/**
 * mic_start - 启动麦克风
 *
 * @returns {bool} 成功返回true,失败返回false
 *
 * 功能说明:
 * 1. 分配PSRAM缓冲区用于音频数据
 * 2. 配置I2S为PDM模式
 * 3. 设置麦克风引脚(CLK和DATA)
 * 4. 安装I2S驱动
 * 5. 清空DMA缓冲区
 */
bool mic_start()
{
    if (mic_running) {
        Serial.println("Microphone already running");
        return true;
    }

    Serial.println("Initializing I2S PDM microphone...");
    Serial.printf("  CLK Pin: GPIO%d\n", MIC_CLK_PIN);
    Serial.printf("  DATA Pin: GPIO%d\n", MIC_DATA_PIN);
    Serial.printf("  Sample Rate: %d Hz\n", MIC_SAMPLE_RATE);

    // 分配缓冲区,优先使用PSRAM以节省内部RAM
    if (i2s_read_buffer == nullptr) {
        i2s_read_buffer = (int16_t *) ps_malloc(MIC_BUFFER_SAMPLES * sizeof(int16_t));
        if (i2s_read_buffer == nullptr) {
            Serial.println("Failed to allocate mic buffer in PSRAM!");
            // PSRAM分配失败,尝试使用常规RAM
            i2s_read_buffer = (int16_t *) malloc(MIC_BUFFER_SAMPLES * sizeof(int16_t));
            if (i2s_read_buffer == nullptr) {
                Serial.println("Failed to allocate mic buffer!");
                return false;
            }
            Serial.println("Using regular RAM for mic buffer");
        } else {
            Serial.println("Using PSRAM for mic buffer");
        }
    }

    // I2S配置:PDM麦克风模式
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM), // 主模式、接收、PDM模式
        .sample_rate = MIC_SAMPLE_RATE,                                       // 采样率16kHz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,                        // 16位采样
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,                         // 单声道(左声道)
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,                   // 标准I2S格式
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,                            // 中断优先级
        .dma_buf_count = 8,                                                   // DMA缓冲区数量
        .dma_buf_len = 256,                                                   // DMA缓冲区长度
        .use_apll = false,                                                    // 不使用APLL
        .tx_desc_auto_clear = false,                                          // 不自动清除TX描述符
        .fixed_mclk = 0,                                                      // 不使用固定MCLK
    };

    // I2S引脚配置:XIAO ESP32S3 Sense板载PDM麦克风
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_PIN_NO_CHANGE,    // BCK不使用
        .ws_io_num = MIC_CLK_PIN,           // WS引脚用作PDM CLK(GPIO42)
        .data_out_num = I2S_PIN_NO_CHANGE,  // 不需要输出
        .data_in_num = MIC_DATA_PIN,        // 数据输入引脚(GPIO41)
    };

    // 安装I2S驱动
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("Failed to install I2S driver: %s\n", esp_err_to_name(err));
        return false;
    }

    // 设置I2S引脚
    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("Failed to set I2S pins: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    // 清空DMA缓冲区,避免初始杂音
    i2s_zero_dma_buffer(I2S_PORT);

    mic_running = true;
    Serial.println("Microphone started successfully");
    return true;
}

/**
 * mic_stop - 停止麦克风
 *
 * 停止I2S驱动并卸载
 */
void mic_stop()
{
    if (!mic_running) {
        return;
    }

    Serial.println("Stopping microphone...");

    i2s_stop(I2S_PORT);
    i2s_driver_uninstall(I2S_PORT);

    mic_running = false;
    Serial.println("Microphone stopped");
}

/**
 * mic_is_running - 检查麦克风是否运行
 *
 * @returns {bool} 运行中返回true,否则返回false
 */
bool mic_is_running()
{
    return mic_running;
}

/**
 * mic_set_callback - 设置音频数据回调函数
 *
 * @param {mic_data_handler} callback - 回调函数指针
 *
 * 当有新的音频数据时,会调用此回调函数
 */
void mic_set_callback(mic_data_handler callback)
{
    audio_callback = callback;
}

/**
 * mic_process - 处理麦克风数据
 *
 * 需要在主循环中定期调用此函数
 * 功能说明:
 * 1. 从I2S读取音频数据
 * 2. 应用增益系数(MIC_GAIN)
 * 3. 限幅处理防止溢出
 * 4. 通过回调函数传递数据
 */
void mic_process()
{
    if (!mic_running || i2s_read_buffer == nullptr) {
        return;
    }

    size_t bytes_read = 0;
    // 从I2S读取数据,超时时间20ms
    esp_err_t err =
        i2s_read(I2S_PORT, i2s_read_buffer, MIC_BUFFER_SAMPLES * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(20));

    if (err == ESP_OK && bytes_read > 0) {
        size_t samples_read = bytes_read / sizeof(int16_t);

        // 应用增益(如果需要)
        if (MIC_GAIN != 1) {
            for (size_t i = 0; i < samples_read; i++) {
                int32_t sample = (int32_t) i2s_read_buffer[i] * MIC_GAIN;
                // 限幅到16位范围(-32768 ~ 32767)
                if (sample > 32767)
                    sample = 32767;
                if (sample < -32768)
                    sample = -32768;
                i2s_read_buffer[i] = (int16_t) sample;
            }
        }

        // 通过回调函数传递音频数据
        if (audio_callback != nullptr) {
            audio_callback(i2s_read_buffer, samples_read);
        }
    }
}
