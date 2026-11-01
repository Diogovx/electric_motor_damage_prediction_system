#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void)
{
    int i = 0;
    while (true) {
        printf("vivo %d\n", i++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}