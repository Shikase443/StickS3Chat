#include "app.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void) {
    static App app; app.begin();
    while(true){app.update();vTaskDelay(pdMS_TO_TICKS(10));}
}
