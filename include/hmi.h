/**
 * @file hmi.h
 * @brief Control de la interfaz de usuario y LEDs de diagnóstico (Kilby UTP).
 */

#ifndef HMI_H
#define HMI_H

#include <Arduino.h>
#include "config_pins.h"

void inicializarHMI();
void toggleKeepAlive();
void setLedKeepAlive(bool estado);
void setLedErrorRTC(bool error);
void setLedErrorDHTI(bool error);
void setLedErrorDHTE(bool error);
void setLedErrorSD(bool error);
void secuenciaArranqueLeds();

#endif // HMI_H
