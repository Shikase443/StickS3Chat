#include "ir_api.hpp"
#include "M5Unified.h"

void IrApi::configure(const Settings& s) {
    IrSettings next = s.ir;
    bool tx_gpio_changed = !engine_.txReady() || irTxGpio(next.tx_mode) != irTxGpio(settings_.tx_mode);
    bool rx_gpio_changed = !engine_.rxReady() || irRxGpio(next.rx_mode) != irRxGpio(settings_.rx_mode);

    if (tx_gpio_changed || rx_gpio_changed) {
        engine_.deinit();
        if (next.enabled) {
            engine_.initTx(irTxGpio(next.tx_mode));
            engine_.initRx(irRxGpio(next.rx_mode));
        }
    } else if (next.enabled && !engine_.txReady()) {
        engine_.initTx(irTxGpio(next.tx_mode));
        engine_.initRx(irRxGpio(next.rx_mode));
    }

    settings_ = next;
}
