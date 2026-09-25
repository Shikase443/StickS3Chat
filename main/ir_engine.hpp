#pragma once
#include "ir_types.hpp"
#include "esp_err.h"

// RMTによるIR送信・受信のハードウェア層。
// 送信は専用キュー+ワーカータスクで非同期、受信はブロック方式。
class IrEngine {
public:
    // 送信チャネルを初期化（gpio は送信用）。
    bool initTx(int gpio);
    // 受信チャネルを初期化（gpio は受信用）。
    bool initRx(int gpio);
    void deinit();

    // 送信リクエストをキューへ投入。満杯なら false。
    bool enqueue(const IrTxRequest& request);

    // 学習（受信）。timeout_ms 待ってRAW信号を result へ。
    // 戻り値: true=受信成功, false=タイムアウト/エラー。
    bool learn(uint32_t timeout_ms, IrLearnResult& result);

    bool txReady() const { return tx_ready_; }
    bool rxReady() const { return rx_ready_; }

private:
    static void workerTask(void*);
    bool transmitRaw(const IrTxRequest& s);

    bool tx_ready_ = false;
    bool rx_ready_ = false;
    void* tx_channel_ = nullptr;   // rmt_channel_handle_t
    void* tx_encoder_ = nullptr;   // rmt_encoder_handle_t
    void* tx_queue_ = nullptr;     // QueueHandle_t
    void* rx_channel_ = nullptr;   // rmt_channel_handle_t
    void* rx_done_queue_ = nullptr;// QueueHandle_t
    void* rx_lock_ = nullptr;      // SemaphoreHandle_t
};
