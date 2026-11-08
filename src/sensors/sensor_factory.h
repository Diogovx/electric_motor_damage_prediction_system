#pragma once
#include "i_sensor.h"

enum class SensorType { ADXL345, INA219 };

class SensorFactory {
public:
    ISensor* createSensor(SensorType type);     // quem recebe e dono do ponteiro
};