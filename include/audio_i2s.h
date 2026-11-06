/**
 * @file audio_i2s.h
 * @brief Adquisición bioacústica con micrófono INMP441 mediante I2S y DMA nativo.
 */

#ifndef AUDIO_I2S_H
#define AUDIO_I2S_H

#include <Arduino.h>
#include "config_pins.h"
#include "globals.h"

enum class TipoGrabacionAudio {
    NORMAL,
    PIR
};

bool inicializarAudioI2S();
bool grabarAudioWAV(TipoGrabacionAudio tipo, const char *fechaHoraStr);
void escribirCabeceraWAV(Stream &archivo, uint32_t totalSamples, uint32_t sampleRate, uint16_t bitsPorMuestra);

// Tarea FreeRTOS para procesar el audio en segundo plano (Core 1)
void tareaProcesamientoAudio(void *pvParameters);

#endif // AUDIO_I2S_H
