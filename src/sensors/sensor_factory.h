#pragma once
#include "i_sensor.h"

// Para adicionar um sensor: novo valor aqui + uma linha em REGISTRY (sensor_factory.cpp)
enum class SensorType { ADXL345, INA219 };

class SensorFactory {
public:
    ISensor* createSensor(SensorType type);     // quem recebe e dono do ponteiro
};

// Apoio ao config.json (funcoes livres: nao alteram a classe do diagrama)
bool sensorTypeFromName(const char* name, SensorType& out);
int  sensorChannels(SensorType type);           // floats que o adapter escreve em readData()
