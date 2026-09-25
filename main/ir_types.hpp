#pragma once
#include <cstdint>
#include <string>

// IR 送受信のモード。Internal=内蔵IR、External=Grove外付けIR。
enum class IrMode { INTERNAL, EXTERNAL };

// 送信・受信それぞれに Internal/External を独立に選択できる。
struct IrSettings {
    bool enabled = false;
    IrMode tx_mode = IrMode::EXTERNAL;  // 既定: 外付けIrTX(GPIO9)
    IrMode rx_mode = IrMode::INTERNAL;  // 既定: 内蔵受光部(GPIO42)
    uint16_t api_port = 8080;
    std::string bearer_token;
};

// モード別のGPIO割り当て（StickS3）。
//   Internal : TX=GPIO46, RX=GPIO42
//   External : TX=GPIO9,  RX=GPIO10
inline constexpr int kIrTxGpioInternal = 46;
inline constexpr int kIrTxGpioExternal = 9;
inline constexpr int kIrRxGpioInternal = 42;
inline constexpr int kIrRxGpioExternal = 10;

inline int irTxGpio(IrMode m) { return m == IrMode::INTERNAL ? kIrTxGpioInternal : kIrTxGpioExternal; }
inline int irRxGpio(IrMode m) { return m == IrMode::INTERNAL ? kIrRxGpioInternal : kIrRxGpioExternal; }

// 受信で保持できる最大タイミング数（mark/space交互）。
inline constexpr uint16_t kIrMaxDurations = 1024;

// 送信リクエスト。durations_us は点灯・消灯を交互に並べたマイクロ秒配列。
struct IrTxRequest {
    uint32_t carrier_hz = 38000;
    uint8_t repeat = 1;
    uint16_t duration_count = 0;
    uint16_t durations_us[kIrMaxDurations];
};

// 学習結果。durations_us は受信したRAW信号（mark/space交互）。
struct IrLearnResult {
    uint16_t duration_count = 0;
    uint16_t durations_us[kIrMaxDurations];
};
