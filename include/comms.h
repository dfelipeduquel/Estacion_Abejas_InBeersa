/**
 * @file comms.h
 * @brief Capa de comunicaciones: WiFi persistente con escaneo de diagnóstico, Ubidots y Fallback AP.
 */

#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include "globals.h"
#include "config_pins.h"

void inicializarComunicaciones();
void escanearRedesWiFiDiagnostico();
void procesarComunicaciones();
bool publicarUbidotsMQTT(const TelemetriaInBeersa &datos);
bool publicarHiveMQFallback(const TelemetriaInBeersa &datos, char *jsonBufferOut, size_t bufferSize);
void atenderServidorWebLocal(const TelemetriaInBeersa &datos);
String obtenerIPLocal();
bool estaEnModoFallbackAP();

#endif // COMMS_H
