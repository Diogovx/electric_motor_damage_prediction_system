#include "model_store.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

#include <string.h>

namespace
{
    constexpr char TAG[] = "MODEL";

    constexpr uint8_t SUBTYPE_BASE = 0x40;
    constexpr uint32_t MAGIC = 0x314C444D;  // "MDL1"
    constexpr size_t HEADER_SIZE = 16;

    struct BlobHeader
    {
        uint32_t magic;
        uint32_t version;
        uint32_t size;
        uint32_t crc32;
    };

    esp_partition_mmap_handle_t handles[2];
    bool mapped[2] = {false, false};
}

bool modelStoreOpen(uint8_t slot, ModelInfo& info)
{
    if (slot > 1)
    {
        return false;
    }

    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(SUBTYPE_BASE + slot),
        nullptr);

    if (part == nullptr)
    {
        ESP_LOGE(TAG, "Particao do slot %u nao encontrada (partitions.csv aplicado?)", slot);
        return false;
    }

    if (mapped[slot])
    {
        esp_partition_munmap(handles[slot]);
        mapped[slot] = false;
    }

    const void* base = nullptr;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                           &base, &handles[slot]) != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao mapear o slot %u", slot);
        return false;
    }
    mapped[slot] = true;

    BlobHeader hdr;
    memcpy(&hdr, base, sizeof(hdr));

    if (hdr.magic != MAGIC)
    {
        ESP_LOGW(TAG, "Slot %u vazio ou sem cabecalho (magic 0x%08lx)",
                 slot, static_cast<unsigned long>(hdr.magic));
        return false;
    }

    if (hdr.size < 64 || hdr.size > part->size - HEADER_SIZE)
    {
        ESP_LOGE(TAG, "Slot %u com tamanho invalido: %lu",
                 slot, static_cast<unsigned long>(hdr.size));
        return false;
    }

    const uint8_t* data = static_cast<const uint8_t*>(base) + HEADER_SIZE;

    if (memcmp(data + 4, "TFL3", 4) != 0)
    {
        ESP_LOGE(TAG, "Slot %u: nao parece um modelo TFLite (sem TFL3)", slot);
        return false;
    }

    const uint32_t crc = esp_rom_crc32_le(0, data, hdr.size);
    if (crc != hdr.crc32)
    {
        ESP_LOGE(TAG, "Slot %u: CRC32 diferente (calculado 0x%08lx, esperado 0x%08lx)",
                 slot, static_cast<unsigned long>(crc),
                 static_cast<unsigned long>(hdr.crc32));
        return false;
    }

    info.data = data;
    info.size = hdr.size;
    info.version = hdr.version;
    info.crc32 = crc;

    ESP_LOGI(TAG, "Slot %u: versao %lu, %lu bytes, CRC32 0x%08lx OK",
             slot,
             static_cast<unsigned long>(info.version),
             static_cast<unsigned long>(info.size),
             static_cast<unsigned long>(info.crc32));

    return true;
}