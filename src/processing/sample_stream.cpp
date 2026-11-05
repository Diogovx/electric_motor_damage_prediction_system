#include "sample_stream.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace
{
    constexpr char TAG[] = "STREAM";
    constexpr UBaseType_t QUEUE_LENGTH = 128;  // ~1,3 s de folga a 100 Hz

    QueueHandle_t queue = nullptr;
    volatile uint32_t dropped = 0;
}

bool sampleStreamInit()
{
    if (queue != nullptr)
    {
        return true;
    }

    queue = xQueueCreate(QUEUE_LENGTH, sizeof(SampleFrame));
    if (queue == nullptr)
    {
        ESP_LOGE(TAG, "Falha ao criar a fila de amostras");
        return false;
    }

    return true;
}

bool sampleStreamPush(const SampleFrame& frame)
{
    if (xQueueSend(queue, &frame, 0) != pdTRUE)
    {
        dropped = dropped + 1;
        return false;
    }
    return true;
}

bool sampleStreamPop(SampleFrame& frame, uint32_t timeoutMs)
{
    return xQueueReceive(queue, &frame, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

uint32_t sampleStreamDropped()
{
    return dropped;
}