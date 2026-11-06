/**
 * @file comms.h
 * @brief Capa de comunicaciones: WiFi persistente, Ubidots Cloud y Web Server local.
 */

#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include "globals.h"
#include "config_pins.h"

void inicializarComunicaciones();
void procesarComunicaciones();
bool publicarUbidotsMQTT(const TelemetriaInBeersa &datos);
bool publicarHiveMQFallback(const TelemetriaInBeersa &datos, char *jsonBufferOut, size_t bufferSize);
void atenderServidorWebLocal(const TelemetriaInBeersa &datos);
String obtenerIPLocal();

#endif // COMMS_H
