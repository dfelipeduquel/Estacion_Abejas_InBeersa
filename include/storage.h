/**
 * @file storage.h
 * @brief Capa de persistencia para tarjeta MicroSD (SPI) y LittleFS secundario.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "globals.h"
#include "config_pins.h"

bool inicializarAlmacenamiento();
bool esSDDisponible();
bool guardarTelemetriaJSON(const char *jsonString);
bool asegurarEstructuraCarpetasSD();

#endif // STORAGE_H
