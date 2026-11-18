#include "sensor_factory.h"
#include "adxl345_adapter.h"
#include "ina219_adapter.h"

#include <string.h>

namespace
{
    struct Entry
    {
        const char* name;               // valor de "type" no config.json
        SensorType type;
        int channels;
        ISensor* (*create)();
    };

    ISensor* makeAdxl345() { return new ADXL345Adapter(); }
    ISensor* makeIna219()  { return new INA219Adapter(); }

    // PONTO DE EXTENSAO: um sensor novo entra com uma linha aqui
    const Entry REGISTRY[] = {
        {"ADXL345", SensorType::ADXL345, 3, makeAdxl345},
        {"INA219",  SensorType::INA219,  1, makeIna219},
    };
}

ISensor* SensorFactory::createSensor(SensorType type)
{
    for (const Entry& e : REGISTRY)
    {
        if (e.type == type) return e.create();
    }
    return nullptr;
}

bool sensorTypeFromName(const char* name, SensorType& out)
{
    for (const Entry& e : REGISTRY)
    {
        if (strcmp(e.name, name) == 0)
        {
            out = e.type;
            return true;
        }
    }
    return false;
}

int sensorChannels(SensorType type)
{
    for (const Entry& e : REGISTRY)
    {
        if (e.type == type) return e.channels;
    }
    return 0;
}
