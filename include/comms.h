/**
 * @file comms.h
 * @brief Capa de comunicaciones asíncronas WiFi y protocolo MQTT (HiveMQ).
 */

#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include "globals.h"
#include "config_pins.h"

void inicializarComunicaciones();
void verificarConectividadWiFi();
bool publicarTelemetriaMQTT(const TelemetriaInBeersa &datos, char *jsonBufferOut, size_t bufferSize);
void procesarBucleMQTT();

#endif // COMMS_H
