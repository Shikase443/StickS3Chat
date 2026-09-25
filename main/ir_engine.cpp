#include "ir_engine.hpp"
#include <cstring>
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"


// 受信シンボルバッファ（1ワード=mark+space 2タイミング）。
static constexpr size_t kRxSymbolCount = kIrMaxDurations / 2;
static constexpr int kRxResolutionHz = 500000;   // 2us/tick
static constexpr uint32_t kRxTickUs = 2;
// フレーム区切り用の信号範囲。フレーム内ギャップ(<10ms)は1バーストに、
// フレーム間ギャップ(>10ms)はバーストを区切る。
static constexpr uint32_t kRxSignalRangeMaxNs = 10000000;  // 10ms
// 後続フレーム待ちのギャップ閾値。これ以上の間隔で受信が止まれば信号完了。
static constexpr uint32_t kFrameGapMs = 80;
static rmt_symbol_word_t g_rx_symbols[kRxSymbolCount];

namespace {
bool rxDoneCb(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t* event, void* user) {
    (void)ch;
    BaseType_t wake = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user, event, &wake);
    return wake == pdTRUE;
}
}  // namespace

bool IrEngine::initTx(int gpio) {
    if (tx_ready_) return true;
    QueueHandle_t q = xQueueCreate(4, sizeof(IrTxRequest));
    if (!q) return false;
    rmt_tx_channel_config_t cfg{};
    cfg.gpio_num = static_cast<gpio_num_t>(gpio);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = 1000000;
    cfg.mem_block_symbols = 64;
    cfg.trans_queue_depth = 4;
    cfg.flags.invert_out = false;
    cfg.flags.with_dma = false;
    rmt_channel_handle_t ch = nullptr;
    if (rmt_new_tx_channel(&cfg, &ch) != ESP_OK) { vQueueDelete(q); return false; }
    rmt_encoder_handle_t enc = nullptr;
    rmt_copy_encoder_config_t ecfg{};
    if (rmt_new_copy_encoder(&ecfg, &enc) != ESP_OK) { rmt_del_channel(ch); vQueueDelete(q); return false; }
    if (rmt_enable(ch) != ESP_OK) { rmt_del_encoder(enc); rmt_del_channel(ch); vQueueDelete(q); return false; }
    tx_channel_ = ch; tx_encoder_ = enc; tx_queue_ = q;
    if (xTaskCreate(workerTask, "ir_tx_worker", 6144, this, 10, nullptr) != pdPASS) { deinit(); return false; }
    tx_ready_ = true;
    return true;
}

bool IrEngine::initRx(int gpio) {
    if (rx_ready_) return true;
    QueueHandle_t dq = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    if (!dq || !lock) { if (dq) vQueueDelete(dq); if (lock) vSemaphoreDelete(lock); return false; }
    rmt_rx_channel_config_t cfg{};
    cfg.gpio_num = static_cast<gpio_num_t>(gpio);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = kRxResolutionHz;
    cfg.mem_block_symbols = 64;
    cfg.flags.with_dma = true;
    rmt_channel_handle_t ch = nullptr;
    if (rmt_new_rx_channel(&cfg, &ch) != ESP_OK) { vQueueDelete(dq); vSemaphoreDelete(lock); return false; }
    rmt_rx_event_callbacks_t cbs{};
    cbs.on_recv_done = rxDoneCb;
    if (rmt_rx_register_event_callbacks(ch, &cbs, dq) != ESP_OK) { rmt_del_channel(ch); vQueueDelete(dq); vSemaphoreDelete(lock); return false; }
    if (rmt_enable(ch) != ESP_OK) { rmt_del_channel(ch); vQueueDelete(dq); vSemaphoreDelete(lock); return false; }
    rx_channel_ = ch; rx_done_queue_ = dq; rx_lock_ = lock;
    rx_ready_ = true;
    return true;
}

void IrEngine::deinit() {
    if (tx_channel_) { rmt_disable(static_cast<rmt_channel_handle_t>(tx_channel_)); rmt_del_channel(static_cast<rmt_channel_handle_t>(tx_channel_)); tx_channel_ = nullptr; }
    if (tx_encoder_) { rmt_del_encoder(static_cast<rmt_encoder_handle_t>(tx_encoder_)); tx_encoder_ = nullptr; }
    if (tx_queue_) { vQueueDelete(static_cast<QueueHandle_t>(tx_queue_)); tx_queue_ = nullptr; }
    if (rx_channel_) { rmt_disable(static_cast<rmt_channel_handle_t>(rx_channel_)); rmt_del_channel(static_cast<rmt_channel_handle_t>(rx_channel_)); rx_channel_ = nullptr; }
    if (rx_done_queue_) { vQueueDelete(static_cast<QueueHandle_t>(rx_done_queue_)); rx_done_queue_ = nullptr; }
    if (rx_lock_) { vSemaphoreDelete(static_cast<SemaphoreHandle_t>(rx_lock_)); rx_lock_ = nullptr; }
    tx_ready_ = false; rx_ready_ = false;
}

bool IrEngine::transmitRaw(const IrTxRequest& s) {
    if (s.duration_count < 2) return false;
    size_t segment_count = 0;
    for (size_t i = 0; i < s.duration_count; i++) {
        if (!s.durations_us[i]) return false;
        segment_count += (s.durations_us[i] + 32766U) / 32767U;
    }
    size_t count = (segment_count + 1) / 2;
    auto* symbols = static_cast<rmt_symbol_word_t*>(calloc(count, sizeof(rmt_symbol_word_t)));
    if (!symbols) return false;
    size_t seg = 0;
    for (size_t i = 0; i < s.duration_count; i++) {
        uint32_t remaining = s.durations_us[i];
        uint32_t level = (i % 2) == 0 ? 1 : 0;
        while (remaining) {
            uint32_t duration = remaining > 32767 ? 32767 : remaining;
            size_t word = seg / 2;
            if ((seg & 1) == 0) { symbols[word].level0 = level; symbols[word].duration0 = duration; }
            else { symbols[word].level1 = level; symbols[word].duration1 = duration; }
            remaining -= duration; seg++;
        }
    }
    if (segment_count & 1) { symbols[count - 1].level1 = symbols[count - 1].level0; symbols[count - 1].duration1 = 1; }
    rmt_carrier_config_t carrier{};
    carrier.frequency_hz = s.carrier_hz;
    carrier.duty_cycle = 0.33f;
    carrier.flags.polarity_active_low = false;
    auto* ch = static_cast<rmt_channel_handle_t>(tx_channel_);
    auto* enc = static_cast<rmt_encoder_handle_t>(tx_encoder_);
    esp_err_t err = rmt_apply_carrier(ch, &carrier);
    rmt_transmit_config_t tx{};
    tx.loop_count = 0;
    if (err == ESP_OK) err = rmt_transmit(ch, enc, symbols, count * sizeof(rmt_symbol_word_t), &tx);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(ch, 1000);
    free(symbols);
    return err == ESP_OK;
}

void IrEngine::workerTask(void* arg) {
    auto* self = static_cast<IrEngine*>(arg);
    IrTxRequest s;
    while (xQueueReceive(static_cast<QueueHandle_t>(self->tx_queue_), &s, portMAX_DELAY) == pdTRUE) {
        bool ok = true;
        for (uint8_t i = 0; i < s.repeat && ok; i++) ok = self->transmitRaw(s);
    }
}

bool IrEngine::enqueue(const IrTxRequest& request) {
    if (!tx_ready_) return false;
    return xQueueSend(static_cast<QueueHandle_t>(tx_queue_), &request, 0) == pdTRUE;
}

bool IrEngine::learn(uint32_t timeout_ms, IrLearnResult& result) {
    if (!rx_ready_ || !timeout_ms) return false;
    auto* lock = static_cast<SemaphoreHandle_t>(rx_lock_);
    if (xSemaphoreTake(lock, 0) != pdTRUE) return false;  // 同時学習は拒否（呼び出し側が409）
    auto* ch = static_cast<rmt_channel_handle_t>(rx_channel_);
    auto* dq = static_cast<QueueHandle_t>(rx_done_queue_);
    xQueueReset(dq);
    std::memset(&result, 0, sizeof(result));

    rmt_receive_config_t rcfg{};
    rcfg.signal_range_min_ns = 2000;
    rcfg.signal_range_max_ns = kRxSignalRangeMaxNs;

    int64_t start_us = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;
    int64_t last_burst_us = start_us;
    bool got_any = false;

    auto appendBurst = [&](const rmt_rx_done_event_data_t& event) {
        for (size_t i = 0; i < event.num_symbols && result.duration_count < kIrMaxDurations; i++) {
            const auto* sym = &event.received_symbols[i];
            uint32_t dur[2] = { sym->duration0 * kRxTickUs, sym->duration1 * kRxTickUs };
            for (int h = 0; h < 2 && result.duration_count < kIrMaxDurations; h++) {
                if (!dur[h]) continue;
                result.durations_us[result.duration_count++] = static_cast<uint16_t>(dur[h]);
            }
        }
    };

    for (;;) {
        int64_t now = esp_timer_get_time();
        if (now - start_us >= timeout_us) break;  // 総タイムアウト
        // 次のバースト受信を開始（非ブロック）。
        if (rmt_receive(ch, g_rx_symbols, sizeof(g_rx_symbols), &rcfg) != ESP_OK) break;
        // 後続フレーム待ち：直前バーストから kFrameGapMs 超で信号完了。
        int64_t wait_us = (last_burst_us + (int64_t)kFrameGapMs * 1000) - now;
        if (wait_us <= 0) wait_us = 1000;
        if (wait_us > timeout_us - (now - start_us)) wait_us = timeout_us - (now - start_us);
        if (wait_us <= 0) break;
        TickType_t wait_ticks = pdMS_TO_TICKS((uint32_t)(wait_us / 1000));
        if (wait_ticks < 1) wait_ticks = 1;
        rmt_rx_done_event_data_t event;
        if (xQueueReceive(dq, &event, wait_ticks) != pdTRUE) break;  // フレーム間ギャップで完了
        int64_t burst_us = esp_timer_get_time();
        if (burst_us - start_us >= timeout_us) break;
        appendBurst(event);
        got_any = true;
        last_burst_us = burst_us;
        xQueueReset(dq);
    }

    bool ok = got_any && result.duration_count >= 4;
    xSemaphoreGive(lock);
    return ok;
}
