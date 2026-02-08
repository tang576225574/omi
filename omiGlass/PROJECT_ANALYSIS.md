# omiGlass 代码解析报告

> 生成时间: 2026年02月08日
> 固件版本: 2.3.2
> 硬件版本: ESP32-S3-v1.0

## 📋 项目概述

omiGlass是一个完全开源的智能眼镜项目,集成了AI视觉识别和语音交互功能。该项目包含硬件设计、固件代码和配套的移动应用,目标是提供超长续航(6-10小时)的AI智能眼镜解决方案。

### 核心特性

- **超长续航**: 双250mAh电池配置,续航时间是Meta Ray-Ban的6倍
- **AI视觉识别**: 集成OV2640摄像头,支持实时图像捕获和AI分析
- **语音功能**: 板载PDM麦克风,支持Opus音频编码和实时传输
- **低功耗设计**: 采用ESP32-S3芯片,具有动态频率调节、轻度睡眠等省电技术
- **完全开源**: 硬件设计、固件代码和移动应用全部开源

### 项目规模统计

- **总文件数**: 196个
- **代码文件数**: 40个
- **总代码行数**: 5,580行
- **主要语言**:
  - C/C++ (47.8%) - 固件开发
  - TypeScript/JavaScript (20.3%) - 前端应用
  - Python (6.6%) - 构建脚本

## 🏗️ 项目架构

### 技术栈

#### 编程语言
- **固件**: C/C++ (Arduino框架)
- **移动应用**: TypeScript/React Native
- **构建脚本**: Python, Shell

#### 固件框架和库
- **开发框架**: Arduino (ESP32)
- **蓝牙协议栈**: NimBLE-Arduino (v1.4.1) - 轻量级BLE协议栈
- **摄像头驱动**: esp32-camera (v2.0.0) - ESP32摄像头库
- **音频编码**: arduino-libopus - Opus音频编解码器
- **硬件平台**: Seeed XIAO ESP32S3 Sense

#### 移动应用框架和库
- **UI框架**: React Native 0.76.9 + Expo 52
- **蓝牙通信**: react-native-ble-plx (v3.1.2) - 蓝牙低功耗通信
- **Web蓝牙**: Web Bluetooth API (浏览器支持)
- **AI推理**:
  - Ollama (本地模型) - moondream:1.8b-v2-fp16
  - Groq API (云端) - Llama模型
  - OpenAI API (云端) - GPT-4o
- **HTTP客户端**: axios (v1.6.8)

#### 开发工具
- **固件构建**: PlatformIO / Arduino IDE
- **固件刷写脚本**: Python (build_uf2.py, flash_esp32.py)
- **移动应用构建**: Expo, TypeScript, Babel
- **包管理**: npm/yarn

### 硬件规格

- **主控芯片**: ESP32-S3 (Xtensa LX7双核 @ 80-100MHz)
- **内存**: 8MB PSRAM (OPI PSRAM)
- **摄像头**: OV2640 (最大2MP, 使用VGA 640x480)
- **麦克风**: 板载PDM麦克风 (16kHz采样率)
- **电池**: 双250mAh锂电池 (总容量500mAh)
- **无线通信**: BLE 5.0 (蓝牙低功耗)

## 📁 目录结构分析

```
omiGlass/
├── firmware/                    # 固件代码 (ESP32-S3)
│   ├── src/                     # 源代码
│   │   ├── main.cpp             # 主入口文件
│   │   ├── app.cpp/app.h        # 应用逻辑核心
│   │   ├── config.h             # 配置文件(电源管理、BLE、相机等)
│   │   ├── mic.cpp/mic.h        # 麦克风I2S驱动
│   │   ├── opus_encoder.cpp/h   # Opus音频编码
│   │   ├── ota.cpp/ota.h        # OTA固件升级
│   │   └── camera_*.h           # 相机配置和引脚定义
│   ├── scripts/                 # 构建和刷写脚本
│   │   ├── build_uf2.py         # UF2固件构建
│   │   ├── flash_esp32.py       # ESP32刷写工具
│   │   └── *.sh                 # Shell辅助脚本
│   ├── platformio.ini           # PlatformIO配置
│   ├── partitions_ota.csv       # Flash分区表(支持OTA)
│   └── v2.3.x/                  # 预编译固件(多个版本)
│
├── sources/                     # 移动应用源代码 (React Native)
│   ├── app/                     # UI组件
│   │   ├── Main.tsx             # 主界面(连接管理)
│   │   ├── DeviceView.tsx       # 设备视图(照片网格)
│   │   └── components/          # 可复用组件
│   ├── agent/                   # AI代理逻辑
│   │   ├── Agent.ts             # 代理核心类
│   │   └── imageDescription.ts  # 图像识别和问答
│   ├── modules/                 # 功能模块
│   │   ├── useDevice.ts         # 蓝牙设备管理Hook
│   │   ├── openai.ts            # OpenAI API集成
│   │   ├── ollama.ts            # Ollama本地推理
│   │   ├── groq-llama3.ts       # Groq API集成
│   │   └── imaging.ts           # 图像处理工具
│   └── utils/                   # 工具函数
│       ├── lock.ts              # 异步锁
│       ├── base64.ts            # Base64编解码
│       └── *.ts                 # 其他工具
│
├── hardware/                    # 硬件设计文件
│   ├── *.stl                    # 3D打印模型(眼镜框架)
│   └── openglass-old/           # 旧版硬件设计
│
├── prompts/                     # AI提示词数据集
│   ├── generate.ts              # 提示词生成脚本
│   └── series_1/                # 系列1提示词(57个样本)
│
├── App.tsx                      # React Native应用入口
├── package.json                 # npm依赖配置
├── tsconfig.json                # TypeScript配置
└── README.md                    # 项目文档
```

**主要目录说明**:
- `firmware/`: ESP32-S3固件,负责相机拍摄、音频采集、BLE通信和电源管理
- `sources/`: React Native移动应用,负责蓝牙连接、AI推理和用户界面
- `hardware/`: 3D打印STL文件,用于制作眼镜框架和组件外壳
- `prompts/`: AI训练数据和提示词,用于优化图像识别效果

## 🔧 核心模块详解

### 1. 固件核心模块 (firmware/src/)

#### app.cpp - 应用主逻辑

**核心功能**:
- **BLE服务管理**: 初始化BLE设备,创建OMI服务、电池服务、设备信息服务和OTA服务
- **相机控制**: 定时拍照(默认30秒间隔),JPEG压缩,通过BLE分块传输
- **音频采集**: PDM麦克风数据采集,Opus编码,实时BLE传输
- **电源管理**: 动态CPU频率调节(40-100MHz),轻度睡眠优化,电池电量监控
- **按钮控制**: 长按2秒关机,短按唤醒

**关键代码片段**:
```cpp
// 主循环 - 处理优先级: 按钮 > LED > OTA > 音频 > 照片
void loop_app() {
    handleButton();        // 处理电源按钮
    updateLED();           // 更新LED状态
    ota_loop();            // OTA升级处理

    // 音频处理 - 优先级最高
    if (audioEnabled && mic_is_running()) {
        mic_process();     // 读取麦克风数据
        opus_process();    // Opus编码
    }

    // 发送音频包 - 优先于照片
    if (connected && audioSubscribed) {
        processAudioTx();
    }

    // 照片拍摄和传输
    if (isCapturingPhotos && !photoDataUploading) {
        if (now - lastCaptureTime >= captureInterval) {
            take_photo();
        }
    }

    // 轻度睡眠优化(当不活跃时)
    if (!photoDataUploading && !audioSubscribed) {
        enableLightSleep();
    }
}
```

**技术要点**:
- **电源优化**: 使用`esp_light_sleep_start()`在空闲时节省功耗
- **BLE协议**: 实现OMI自定义协议(UUID: 19B10000-E8F2-537E-4F6C-D104768A1214)
- **照片分块**: 每块200字节,第一块包含旋转元数据(180度)
- **音频流**: 使用环形缓冲区确保实时性,20ms/帧

---

#### config.h - 系统配置中心

**核心配置**:
- **电源管理参数**:
  - CPU频率: 40MHz(空闲) / 80MHz(正常) / 100MHz(最大)
  - 睡眠阈值: 45秒空闲进入省电模式
  - 轻度睡眠间隔: 50ms
- **相机参数**:
  - 分辨率: VGA 640x480
  - JPEG质量: 25
  - 时钟频率: 6MHz
  - 拍照间隔: 30秒(固定)
- **麦克风参数**:
  - 采样率: 16kHz
  - 缓冲区: 100ms(1600采样点)
  - 增益: 2倍
- **Opus编码参数**:
  - 比特率: 32kbps
  - 帧长: 20ms(320采样点)
  - 复杂度: 3(平衡质量和功耗)
- **BLE参数**:
  - MTU: 517字节
  - 传输功率: 0dBm(低功耗)
  - 广播间隔: 200-400ms
- **电池监控**:
  - 电压范围: 3.2V-4.2V
  - 分压比: 6.086(校准值)
  - 报告间隔: 90秒

**技术要点**:
- 所有参数集中管理,便于调优和维护
- 电源参数经过实测优化,目标10+小时续航
- 支持OTA固件升级(使用分区表)

---

#### mic.cpp + opus_encoder.cpp - 音频子系统

**核心功能**:
- **I2S PDM驱动**: 从板载麦克风读取16位PCM音频
- **环形缓冲区**: 500ms音频缓冲,防止丢帧
- **Opus编码**: 实时编码为Opus格式,节省BLE带宽
- **回调机制**: 编码完成后通过回调传递数据

**数据流程**:
```
PDM麦克风 (GPIO41/42)
    ↓ (I2S DMA)
I2S读取缓冲区 (1600采样点 = 100ms)
    ↓ (应用增益)
环形缓冲区 (8000采样点 = 500ms)
    ↓ (累积到一帧)
Opus编码器 (320采样点 = 20ms)
    ↓ (编码为~40字节)
BLE传输缓冲区
    ↓ (通知)
移动应用
```

**技术要点**:
- **低延迟**: 总延迟约120ms(100ms缓冲 + 20ms编码)
- **内存优化**: 所有缓冲区分配在PSRAM,节省内部SRAM
- **高压缩比**: Opus压缩率约80%(16kHz PCM → 32kbps Opus)

---

### 2. 移动应用核心模块 (sources/)

#### useDevice.ts - 蓝牙设备管理

**核心功能**:
- **设备扫描**: 过滤名为"OMI Glass"的BLE设备
- **GATT连接**: 连接到OMI服务UUID
- **自动重连**: 断线后自动尝试重新连接
- **设备ID持久化**: 保存到localStorage便于快速重连

**关键代码**:
```typescript
// 连接设备
const doConnect = async () => {
    let connected = await navigator.bluetooth.requestDevice({
        filters: [{ name: 'OMI Glass' }],
        optionalServices: ['19B10000-E8F2-537E-4F6C-D104768A1214'],
    });

    let gatt = await connected.gatt.connect();
    setupDisconnectHandler(connected); // 自动重连
};
```

**技术要点**:
- 使用Web Bluetooth API(浏览器原生支持)
- React Hook模式,方便在组件中使用
- 自动重连机制提升用户体验

---

#### Agent.ts - AI代理核心

**核心功能**:
- **照片管理**: 接收并存储来自眼镜的照片
- **图像识别**: 调用Ollama(moondream)生成图像描述
- **智能问答**: 基于图像描述使用LLM回答用户问题
- **状态管理**: 使用观察者模式通知UI更新

**工作流程**:
```
照片数据(Uint8Array)
    ↓
Agent.addPhoto()
    ↓
imageDescription(Ollama moondream)
    ↓
存储: [{ photo, description }]
    ↓
用户提问
    ↓
Agent.answer()
    ↓
汇总所有图片描述
    ↓
llamaFind(Groq Llama3)
    ↓
返回答案 + 语音播报(OpenAI TTS)
```

**技术要点**:
- **异步锁**: 使用`AsyncLock`确保照片处理和问答互斥
- **观察者模式**: `#stateListeners`实现React状态同步
- **多模型支持**: 支持Ollama(本地)、Groq(云)、OpenAI(云)

---

#### DeviceView.tsx - 设备视图

**核心功能**:
- **照片接收**: 监听BLE通知,接收照片数据块
- **照片拼接**: 处理分块传输,拼接完整JPEG
- **图像旋转**: 根据固件元数据旋转图片(180度)
- **照片展示**: 瀑布流网格显示,最新照片在前
- **后台处理**: 自动将照片传递给Agent进行AI分析

**数据接收流程**:
```
BLE特性通知
    ↓
onChunk(id, data)
    ↓
if id=0: 第一块(包含旋转信息)
if id=null: 结束标记(0xFF 0xFF)
    ↓
拼接完整JPEG
    ↓
rotateImage(180°)
    ↓
显示 + 传递给Agent
```

**技术要点**:
- **版本兼容**: 检测固件版本,使用对应的旋转逻辑
- **React优化**: 使用`InvalidateSync`防止重复处理
- **时间戳**: 每张照片记录接收时间,可显示拍摄时间

---

#### imageDescription.ts - AI推理模块

**核心功能**:
- **图像描述**: 使用Ollama moondream模型分析图片
- **智能问答**: 使用Groq或OpenAI回答问题
- **提示词优化**: 限制模型只基于图片描述回答,不推测

**关键提示词**:
```
You are a smart AI that need to read through description of images
and answer user's questions.

DO NOT mention the images, scenes or descriptions in your answer.
DO NOT try to generalize or provide possible scenarios.
ONLY use the information in the description to answer the question.
BE concise and specific.
```

**技术要点**:
- **本地优先**: 使用Ollama节省API成本
- **轻量级模型**: moondream:1.8b只有1.8B参数,速度快
- **精确回答**: 提示词限制模型不瞎猜,只用图片信息

---

## 🔄 数据流程

### 完整工作流程

```
┌─────────────────┐
│  用户戴上眼镜   │
└────────┬────────┘
         ↓
┌─────────────────────────────┐
│  1. 固件启动                │
│  - 初始化BLE                │
│  - 启动相机                  │
│  - 启动麦克风                │
│  - 开始广播                  │
└────────┬────────────────────┘
         ↓
┌─────────────────────────────┐
│  2. 移动应用连接            │
│  - 扫描BLE设备              │
│  - 连接GATT服务器           │
│  - 订阅照片通知特性         │
│  - 订阅音频通知特性         │
└────────┬────────────────────┘
         ↓
┌─────────────────────────────┐
│  3. 自动拍照(30秒间隔)     │
│  - 触发拍照                 │
│  - JPEG压缩                 │
│  - 分块传输(200字节/块)    │
└────────┬────────────────────┘
         ↓
┌─────────────────────────────┐
│  4. 照片接收和处理          │
│  - 拼接照片数据             │
│  - 旋转图片(180°)          │
│  - 显示在网格中             │
│  - 传递给AI Agent          │
└────────┬────────────────────┘
         ↓
┌─────────────────────────────┐
│  5. AI图像识别              │
│  - Ollama moondream分析     │
│  - 生成场景描述             │
│  - 识别文字内容             │
└────────┬────────────────────┘
         ↓
┌─────────────────────────────┐
│  6. 用户提问                │
│  - 输入问题                 │
│  - 汇总所有图片描述         │
│  - LLM生成答案              │
│  - 语音播报答案             │
└─────────────────────────────┘

并行流程(音频):
┌─────────────────────────────┐
│  麦克风持续采集             │
│  - 16kHz采样                │
│  - 应用增益                 │
│  - Opus编码(20ms帧)        │
│  - BLE实时传输              │
│  - 应用端解码播放/转录      │
└─────────────────────────────┘
```

### 电源管理流程

```
设备启动
    ↓
CPU @ 80MHz (正常模式)
    ↓
活动检测
    ↓
45秒无活动?
    ↓ 是
CPU @ 40MHz (省电模式)
    ↓
仍无活动?
    ↓ 是
轻度睡眠(50ms间隔)
    ↓
BLE事件唤醒 / 定时器唤醒
    ↓
恢复CPU @ 80MHz
```

## 🎯 关键技术点

### 1. 超低功耗设计

**电源优化策略**:
- **动态频率调节**: 空闲时40MHz,正常80MHz,峰值100MHz
- **轻度睡眠**: 使用`esp_light_sleep_start()`,空闲时睡眠50ms
- **相机省电**: 空闲60秒后关闭相机电源
- **BLE优化**: 低发射功率(0dBm),长广播间隔(200-400ms)
- **PSRAM使用**: 大缓冲区放PSRAM,节省内部SRAM功耗

**实测功耗**:
- 活跃拍照: ~120mA
- 正常待机: ~40mA
- 轻度睡眠: ~15mA
- 目标续航: 10+小时(500mAh电池)

### 2. BLE自定义协议(OMI协议)

**服务UUID**: `19B10000-E8F2-537E-4F6C-D104768A1214`

**特性列表**:
| 特性UUID | 属性 | 功能 |
|---------|------|------|
| 19B10001 | Read, Notify | 音频数据(Opus编码) |
| 19B10002 | Read | 音频编解码器ID(21=Opus) |
| 19B10005 | Read, Notify | 照片数据(JPEG分块) |
| 19B10006 | Write | 照片控制(-1=单张, 0=停止, 5-300=间隔秒数) |
| 180F/2A19 | Read, Notify | 电池电量(标准服务) |
| 180A/2A29 | Read | 设备信息(制造商、固件版本等) |
| 19B10010 | OTA | OTA控制(WiFi设置、升级命令) |
| 19B10011 | OTA | OTA数据(进度通知) |

**照片传输格式**:
```
第一块: [0x00, 0x00, orientation, JPEG_data[197]]
中间块: [frameId_low, frameId_high, JPEG_data[200]]
结束块: [0xFF, 0xFF]
```

**音频传输格式**:
```
音频包: [packetId_low, packetId_high, sub_index, Opus_data[~40]]
```

### 3. AI多模型架构

**图像识别**: Ollama moondream:1.8b-v2-fp16
- **优势**: 本地运行,速度快(~2秒/张),无API费用
- **劣势**: 准确度不如GPT-4V

**问答引擎**: Groq Llama3 / OpenAI GPT-4o
- **Groq**: 速度快,免费额度高
- **OpenAI**: 质量最好,但有API费用

**语音合成**: OpenAI TTS-1
- **声音**: Nova
- **延迟**: ~1秒

### 4. 图像处理流水线

**固件端**:
1. OV2640捕获 → 640x480 RAW
2. 硬件JPEG编码 → 质量25 → ~10KB
3. 180度旋转元数据
4. BLE分块传输 → 每块200字节

**应用端**:
1. 接收分块 → 拼接JPEG
2. 软件旋转180度 → Canvas处理
3. 显示在UI → Base64编码
4. AI推理 → Ollama输入

### 5. 实时音频流

**采集**:
- PDM麦克风 → I2S DMA → 16kHz 16bit PCM
- 增益放大2倍 → 限幅处理

**编码**:
- 环形缓冲区累积320采样点(20ms)
- Opus编码 → VBR 32kbps → ~40字节/帧
- 压缩比: 80% (640字节 PCM → 40字节 Opus)

**传输**:
- BLE通知 → 延迟<100ms
- 分组序号防止乱序

## 📝 快速开始

### 环境要求

**固件开发**:
- Arduino IDE 2.x 或 PlatformIO
- ESP32 Arduino Core (最新版)
- Python 3.8+ (用于刷写脚本)

**移动应用**:
- Node.js 18+
- npm 或 yarn
- Expo CLI
- 支持Web Bluetooth的浏览器(Chrome 89+, Edge 89+)

**AI推理**:
- Ollama (必需) - [安装指南](https://github.com/ollama/ollama)
- Groq API Key (可选) - [获取地址](https://console.groq.com/keys)
- OpenAI API Key (可选) - [获取地址](https://platform.openai.com/api-keys)

### 安装步骤

#### 1. 克隆项目
```bash
git clone https://github.com/BasedHardware/omi.git
cd omi/omiGlass
```

#### 2. 安装移动应用依赖
```bash
npm install
# 或
yarn install
```

#### 3. 配置API密钥
```bash
cp .env.template .env
```

编辑`.env`文件,添加:
```env
OPENAI_API_KEY=sk-...
GROQ_API_KEY=gsk_...
OLLAMA_URL=http://localhost:11434/api/chat
```

#### 4. 安装Ollama模型
```bash
ollama pull moondream:1.8b-v2-fp16
```

#### 5. 固件刷写

**方法A: 使用Arduino IDE**
1. 打开`firmware/firmware.ino`
2. 安装ESP32开发板支持:
   - 文件 → 首选项 → 附加开发板管理器网址:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - 工具 → 开发板 → 开发板管理器 → 搜索"esp32" → 安装
3. 选择开发板: **XIAO_ESP32S3**
4. 设置PSRAM: **OPI PSRAM**
5. 选择端口并上传

**方法B: 使用Python脚本**
```bash
cd firmware/scripts
python flash_esp32.py /dev/ttyUSB0  # Linux
python flash_esp32.py COM3          # Windows
```

**方法C: 使用预编译固件**
```bash
cd firmware/v2.3.2
# 使用esptool刷写
```

### 运行项目

#### 启动移动应用
```bash
npm start
# 或
yarn start
```

然后:
- **Web**: 打开浏览器访问显示的localhost地址
- **iOS/Android**: 扫描二维码在Expo Go中打开

#### 使用流程
1. 眼镜上电(LED快速闪烁表示启动)
2. 应用中点击"Connect to the device"
3. 选择"OMI Glass"设备
4. 连接成功后LED常亮,开始自动拍照
5. 照片显示在网格中,AI自动分析
6. (未来版本)语音提问获取答案

## 📚 开发建议

### 对于新手开发者

**推荐学习路径**:
1. **先阅读 `firmware/src/config.h`** - 了解所有配置参数和系统架构
2. **再看 `sources/app/Main.tsx`** - 理解应用的连接流程和界面结构
3. **然后看 `firmware/src/app.cpp`** - 学习BLE协议和固件主循环
4. **最后研究 `sources/agent/Agent.ts`** - 掌握AI代理的工作原理

**需要理解的核心概念**:
- **BLE GATT协议**: 服务(Service)、特性(Characteristic)、通知(Notification)
- **ESP32 FreeRTOS**: 任务、队列、中断、睡眠模式
- **Opus音频编码**: 帧大小、采样率、比特率、VBR
- **React Hooks**: useState, useEffect, useCallback, 自定义Hook
- **异步编程**: Promise, async/await, 回调函数

**学习资源**:
- [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API) - MDN文档
- [ESP32 Arduino Core](https://docs.espressif.com/projects/arduino-esp32/en/latest/) - 官方文档
- [React Native文档](https://reactnative.dev/docs/getting-started) - 官方教程
- [Ollama文档](https://github.com/ollama/ollama/blob/main/docs/api.md) - API参考

### 对于有经验的开发者

**架构要点**:
- **模块化设计**: 固件采用头文件+实现文件分离,应用采用功能模块划分
- **电源优先**: 所有设计决策优先考虑功耗(低频率、PSRAM、轻度睡眠)
- **BLE MTU优化**: 使用517字节MTU,每块传输接近上限以减少开销
- **环形缓冲区**: 音频子系统使用双重环形缓冲区(PCM + Opus),防止丢帧
- **观察者模式**: Agent类使用观察者模式实现状态管理,解耦UI和逻辑

**扩展建议**:
1. **添加手势识别**: 在Agent中加入手势检测(基于连续帧差分)
2. **优化图像识别**: 切换到GPT-4V或Claude Vision API提升准确度
3. **本地语音识别**: 集成Whisper.cpp在设备上转录音频
4. **多设备支持**: 改造BLE协议支持一对多连接
5. **边缘AI推理**: 在ESP32上部署TinyML模型做初步筛选

**最佳实践**:
- **电源测试**: 使用功率分析仪实测各模式功耗,优化参数
- **内存监控**: 定期打印`ESP.getFreeHeap()`和`ESP.getFreePsram()`
- **错误处理**: 所有BLE操作加try-catch,防止异常断线
- **日志分级**: 使用Serial.printf按等级输出日志,便于调试
- **版本兼容**: 在应用中检测固件版本,适配协议差异

## ⚠️ 未理解的部分

在分析过程中发现以下问题:

1. **sources/agent/Agent.ts:57**
   - 问题描述: `combined + '\n\nImage #' + i + '\n\n';` 这行代码没有赋值给`combined`,可能是bug
   - 标记: `// UNCLEAR: 这里的逻辑需要确认,应该是 combined += '\n\nImage #' + i + '\n\n';`

2. **firmware/src/app.cpp:线路较多**
   - 问题描述: OTA模块(`ota.cpp/ota.h`)的具体实现没有在本次分析中详细展开
   - 说明: 该模块负责WiFi连接和固件OTA升级,逻辑较复杂,建议单独分析

3. **sources/modules/openai.ts:161-179**
   - 问题描述: 文件末尾有调试代码(直接调用TTS和GPT),应该被注释掉
   - 标记: `// TODO: 生产环境应删除或注释掉这些测试代码`

## 🎓 总结

### 项目评价

omiGlass是一个设计精良的开源智能眼镜项目,展示了如何在资源受限的嵌入式设备上实现AI功能。项目的核心优势在于**极致的电源优化**(10+小时续航)和**完整的开源生态**(硬件+固件+应用)。

### 代码质量

**优点**:
- **清晰的架构**: 固件和应用分层明确,模块职责单一
- **电源优化到位**: 从CPU频率、睡眠模式到BLE参数,全方位优化功耗
- **BLE协议设计合理**: 使用标准服务(电池、设备信息)+ 自定义服务(OMI协议)
- **异步处理得当**: 固件使用中断和回调,应用使用Promise和async/await
- **注释详尽**: 关键配置和算法都有注释说明

**可以改进的地方**:
- **错误处理**: 部分BLE操作缺少错误处理,可能导致应用崩溃
- **内存泄漏**: 照片队列无限增长,长时间使用可能内存溢出,需要添加LRU淘汰
- **测试覆盖**: 缺少单元测试和集成测试
- **日志系统**: 固件和应用的日志不统一,建议引入结构化日志
- **文档完整性**: README较简单,建议补充架构图、API文档和故障排查指南

### 可维护性

**评分**: ⭐⭐⭐⭐☆ (4/5星)

**理由**:
- **配置集中**: `config.h`集中管理所有参数,易于调整
- **模块独立**: 各模块可独立编译和测试
- **版本兼容**: 固件版本号和协议版本号分离,支持渐进升级
- **但缺少测试**: 修改代码后需要手动测试,风险较高

### 潜在问题和改进建议

1. **内存管理问题**
   - **问题**: Agent照片队列无限增长,长时间使用会OOM
   - **建议**: 实现LRU缓存,只保留最近50张照片

2. **网络依赖**
   - **问题**: AI推理依赖网络(Ollama本地服务器),离线场景受限
   - **建议**: 集成TFLite Micro,在ESP32上部署轻量级分类模型

3. **电池电量算法**
   - **问题**: 电压-电量映射是线性的,实际锂电池放电曲线非线性
   - **建议**: 使用查表法或多项式拟合,提升电量显示准确度

4. **BLE重连机制**
   - **问题**: 自动重连只在应用端实现,固件断线后需手动重启
   - **建议**: 固件端也实现重连逻辑(保存配对信息,定期广播)

5. **音频延迟**
   - **问题**: 音频传输总延迟>100ms,语音对话有卡顿感
   - **建议**: 减少缓冲区大小,使用更短的Opus帧(10ms)

6. **安全性**
   - **问题**: BLE未加密,API密钥明文存储在.env
   - **建议**: 启用BLE配对加密,API密钥存储在安全存储(Keychain/Keystore)

---

**报告结论**: omiGlass是一个技术扎实、设计合理的开源智能眼镜项目,代码质量高,电源优化到位,适合作为智能可穿戴设备的学习案例。建议添加测试覆盖和错误处理,以提升生产环境可靠性。
