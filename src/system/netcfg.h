#pragma once

struct NetConfig
{
    char ssid[33];
    char pass[65];
    char mqttUri[96];
};

// Carrega as credenciais salvas no NVS; se nao houver, usa os padroes do secrets.h.
// Retorna true se vieram do NVS.
bool netcfgLoad(NetConfig& cfg);

// Grava as credenciais no NVS.
bool netcfgSave(const NetConfig& cfg);
