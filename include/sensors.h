/**
 * @file sensors.h
 * @brief Capa de drivers para sensores meteorológicos, gases y RTC.
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "globals.h"
#include "config_pins.h"

bool inicializarSensores();
bool leerDHTInterno(float &temp, float &hum);
bool leerDHTExterno(float &temp, float &hum);
float leerGasesMQ135();
bool actualizarHoraRTC(TelemetriaInBeersa &datos);
bool sincronizarRTCPorNTP();
void configurarInterrupcionesHardware();

// Manejadores ISR rápidos
void IRAM_ATTR isrPIR();
void IRAM_ATTR isrLluvia();

#endif // SENSORS_H
