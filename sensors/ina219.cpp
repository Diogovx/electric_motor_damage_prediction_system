#include "ina219.h"

bool ina219Init()
{
    // Inicialização do INA219
    return true;
}

bool ina219Read(INA219Data& data)
{
    // Leitura do sensor

    data.timestamp = 0;
    data.busVoltage = 0;
    data.shuntVoltage = 0;
    data.current = 0;
    data.power = 0;

    return true;
}

