#include "sensor_factory.h"
#include "adxl345_adapter.h"
#include "ina219_adapter.h"

ISensor* SensorFactory::createSensor(SensorType type)
{
    switch (type)
    {
        case SensorType::ADXL345: return new ADXL345Adapter();
        case SensorType::INA219:  return new INA219Adapter();
    }
    return nullptr;
}