/**
 * Opus音频编码器模块
 *
 * 负责将PCM音频数据编码为Opus格式以通过BLE传输
 * 主要功能:
 * 1. 初始化Opus编码器
 * 2. 接收PCM音频数据到环形缓冲区
 * 3. 编码音频帧(20ms/帧)
 * 4. 通过回调函数传递编码后的数据
 */
#include "opus_encoder.h"

#include <opus.h>
#include <esp_heap_caps.h>

#include "config.h"

// Opus编码器实例
static OpusEncoder *encoder = nullptr;
static opus_encoded_handler encoded_callback = nullptr;  // 编码数据回调函数

// PCM数据环形缓冲区 - 分配在PSRAM中
static int16_t *pcm_ring_buffer = nullptr;
static volatile size_t ring_write_pos = 0;  // 写入位置
static volatile size_t ring_read_pos = 0;   // 读取位置

// 编码缓冲区 - 分配在PSRAM中
static uint8_t *opus_output_buffer = nullptr;  // Opus编码输出缓冲区
static int16_t *opus_input_buffer = nullptr;   // Opus编码输入缓冲区(一帧数据)

/**
 * opus_encoder_init - 初始化Opus编码器
 *
 * @returns {bool} 成功返回true,失败返回false
 *
 * 功能说明:
 * 1. 在PSRAM中分配环形缓冲区和编码缓冲区
 * 2. 创建Opus编码器实例
 * 3. 配置编码参数(比特率、复杂度、VBR等)
 * 4. 初始化环形缓冲区位置
 */
bool opus_encoder_init()
{
    if (encoder != nullptr) {
        Serial.println("Opus encoder already initialized");
        return true;
    }

    Serial.println("Initializing Opus encoder...");

    // 在PSRAM中分配PCM环形缓冲区(500ms音频数据)
    pcm_ring_buffer = (int16_t *)heap_caps_malloc(AUDIO_RING_BUFFER_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (pcm_ring_buffer == nullptr) {
        Serial.println("Failed to allocate PCM ring buffer in PSRAM");
        return false;
    }
    Serial.println("PCM ring buffer allocated in PSRAM");

    // 分配Opus输出缓冲区(编码后的数据)
    opus_output_buffer = (uint8_t *)heap_caps_malloc(OPUS_OUTPUT_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (opus_output_buffer == nullptr) {
        Serial.println("Failed to allocate opus output buffer in PSRAM");
        heap_caps_free(pcm_ring_buffer);
        pcm_ring_buffer = nullptr;
        return false;
    }

    // 分配Opus输入缓冲区(一帧PCM数据:320个采样点)
    opus_input_buffer = (int16_t *)heap_caps_malloc(OPUS_FRAME_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (opus_input_buffer == nullptr) {
        Serial.println("Failed to allocate opus input buffer in PSRAM");
        heap_caps_free(pcm_ring_buffer);
        heap_caps_free(opus_output_buffer);
        pcm_ring_buffer = nullptr;
        opus_output_buffer = nullptr;
        return false;
    }

    // 创建Opus编码器
    // 参数:采样率16kHz, 单声道, VOIP模式
    int error;
    encoder = opus_encoder_create(MIC_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);

    if (error != OPUS_OK || encoder == nullptr) {
        Serial.printf("Failed to create Opus encoder: %d\n", error);
        heap_caps_free(pcm_ring_buffer);
        heap_caps_free(opus_output_buffer);
        heap_caps_free(opus_input_buffer);
        pcm_ring_buffer = nullptr;
        opus_output_buffer = nullptr;
        opus_input_buffer = nullptr;
        return false;
    }

    // 配置编码器参数:优化语音编码质量和功耗
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE));          // 比特率:32kbps
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY));    // 复杂度:3
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));      // 信号类型:语音
    opus_encoder_ctl(encoder, OPUS_SET_VBR(OPUS_VBR));                  // 启用可变比特率
    opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0));              // 不约束VBR
    opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));                  // 16位采样深度
    opus_encoder_ctl(encoder, OPUS_SET_DTX(0));                         // 禁用DTX(不连续传输)
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0));                  // 禁用带内FEC(前向纠错)
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(0));            // 丢包率:0%

    // 重置环形缓冲区
    ring_write_pos = 0;
    ring_read_pos = 0;

    Serial.println("Opus encoder initialized successfully");
    Serial.printf("  Sample rate: %d Hz\n", MIC_SAMPLE_RATE);
    Serial.printf("  Bitrate: %d bps\n", OPUS_BITRATE);
    Serial.printf("  Frame size: %d samples (%d ms)\n", OPUS_FRAME_SAMPLES, OPUS_FRAME_SAMPLES * 1000 / MIC_SAMPLE_RATE);

    return true;
}

/**
 * opus_set_callback - 设置编码数据回调函数
 *
 * @param {opus_encoded_handler} callback - 回调函数指针
 *
 * 当音频帧编码完成后,会调用此回调函数传递编码数据
 */
void opus_set_callback(opus_encoded_handler callback)
{
    encoded_callback = callback;
}

/**
 * opus_receive_pcm - 接收PCM音频数据
 *
 * @param {int16_t*} data - PCM音频数据
 * @param {size_t} samples - 采样点数量
 * @returns {int} 成功返回0,失败返回-1
 *
 * 将PCM数据写入环形缓冲区
 * 如果缓冲区满,会丢弃最旧的数据(覆盖策略)
 */
int opus_receive_pcm(int16_t *data, size_t samples)
{
    if (pcm_ring_buffer == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < samples; i++) {
        size_t next_write = (ring_write_pos + 1) % AUDIO_RING_BUFFER_SAMPLES;
        if (next_write == ring_read_pos) {
            // 缓冲区满,丢弃最旧的采样点
            ring_read_pos = (ring_read_pos + 1) % AUDIO_RING_BUFFER_SAMPLES;
        }
        pcm_ring_buffer[ring_write_pos] = data[i];
        ring_write_pos = next_write;
    }
    return 0;
}

/**
 * ring_buffer_available - 获取环形缓冲区中可用的采样点数
 *
 * @returns {size_t} 可用的采样点数量
 */
static size_t ring_buffer_available()
{
    if (ring_write_pos >= ring_read_pos) {
        return ring_write_pos - ring_read_pos;
    } else {
        return AUDIO_RING_BUFFER_SAMPLES - ring_read_pos + ring_write_pos;
    }
}

/**
 * opus_encode_frame - 编码一帧音频
 *
 * @param {int16_t*} pcm_data - PCM音频数据(320个采样点)
 * @param {size_t} samples - 采样点数量(必须等于OPUS_FRAME_SAMPLES)
 * @returns {int} 成功返回编码后的字节数,失败返回-1
 *
 * 使用Opus编码器将一帧PCM数据编码为Opus格式
 * 帧大小固定为320采样点(20ms @ 16kHz)
 */
int opus_encode_frame(int16_t *pcm_data, size_t samples)
{
    if (encoder == nullptr || opus_output_buffer == nullptr) {
        return -1;
    }

    if (samples != OPUS_FRAME_SAMPLES) {
        Serial.printf("Invalid frame size: %d (expected %d)\n", samples, OPUS_FRAME_SAMPLES);
        return -1;
    }

    // 调用Opus编码函数
    opus_int32 encoded_bytes =
        opus_encode(encoder, pcm_data, OPUS_FRAME_SAMPLES, opus_output_buffer, OPUS_OUTPUT_MAX_BYTES);

    if (encoded_bytes < 0) {
        Serial.printf("Opus encoding error: %d\n", encoded_bytes);
        return -1;
    }

    return encoded_bytes;
}

/**
 * opus_process - 处理音频编码
 *
 * 需要在主循环中定期调用此函数
 * 功能说明:
 * 1. 检查环形缓冲区是否有足够的数据(一帧)
 * 2. 从环形缓冲区读取一帧数据
 * 3. 编码该帧
 * 4. 通过回调函数传递编码后的数据
 */
void opus_process()
{
    if (encoder == nullptr || pcm_ring_buffer == nullptr || opus_input_buffer == nullptr) {
        return;
    }

    // 循环处理所有可用的完整帧
    while (ring_buffer_available() >= OPUS_FRAME_SAMPLES) {
        // 从环形缓冲区读取一帧数据(320个采样点)
        for (size_t i = 0; i < OPUS_FRAME_SAMPLES; i++) {
            opus_input_buffer[i] = pcm_ring_buffer[ring_read_pos];
            ring_read_pos = (ring_read_pos + 1) % AUDIO_RING_BUFFER_SAMPLES;
        }

        // 编码该帧
        int encoded_bytes = opus_encode_frame(opus_input_buffer, OPUS_FRAME_SAMPLES);

        // 如果编码成功且有回调函数,传递编码数据
        if (encoded_bytes > 0 && encoded_callback != nullptr) {
            encoded_callback(opus_output_buffer, encoded_bytes);
        }
    }
}

/**
 * opus_get_codec_id - 获取音频编解码器ID
 *
 * @returns {uint8_t} 编解码器ID(21 = Opus)
 *
 * 此ID用于BLE特性,告知客户端使用的编解码器类型
 */
uint8_t opus_get_codec_id()
{
    // Codec ID 20 = Opus (匹配Omi协议)
    // 实际Omi使用CODEC_ID 21表示Opus
    return AUDIO_CODEC_ID;
}
