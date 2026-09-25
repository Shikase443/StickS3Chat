#pragma once
#include <atomic>
#include "ir_engine.hpp"
#include "ir_types.hpp"
#include "app_types.hpp"

// IR送信/学習のコントローラ。RMTエンジンとアンプ制御を管理。
// HTTP APIはWebServer(ポート80)上で提供される。
class IrApi {
public:
    // 設定に応じてRMTチャネルを初期化/再初期化。
    void configure(const Settings&);
    IrEngine& engine() { return engine_; }
    std::atomic<bool>& learningActive() { return learning_active_; }
    const IrSettings& settings() const { return settings_; }

private:
    IrEngine engine_;
    IrSettings settings_;
    std::atomic<bool> learning_active_{false};
};
