#include "adxl345.h"

bool adxl345Init()
{
    // Inicialização do ADXL345
    return true;
}

bool adxl345Read(ADXL345Data& data)
{
    data.timestamp = 0;

    data.x = 0;
    data.y = 0;
    data.z = 0;

    return true;
}