/**
 * @file audio_i2s.cpp
 * @brief Implementación de captura I2S con buffers DMA en segundo plano.
 */

#include "audio_i2s.h"
#include "storage.h"
#include "hmi.h"
#include <driver/i2s.h>
#include <SD.h>

static const i2s_port_t I2S_PORT = I2S_NUM_0;
static bool i2sInicializado = false;

bool inicializarAudioI2S() {
    if (i2sInicializado) {
        return true;
    }

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = AUDIO_DMA_BUF_COUNT,
        .dma_buf_len = AUDIO_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_SCK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] Fallo al instalar driver I2S: 0x%x\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] Fallo al asignar pines I2S: 0x%x\n", err);
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    i2sInicializado = true;
    Serial.println("[OK] Periférico I2S y descriptores DMA inicializados (INMP441).");
    return true;
}

void escribirCabeceraWAV(Stream &archivo, uint32_t totalSamples, uint32_t sampleRate, uint16_t bitsPorMuestra) {
    uint16_t numChannels = 1; // Mono
    uint32_t byteRate = sampleRate * numChannels * (bitsPorMuestra / 8);
    uint16_t blockAlign = numChannels * (bitsPorMuestra / 8);
    uint32_t dataChunkSize = totalSamples * blockAlign;
    uint32_t riffChunkSize = 36 + dataChunkSize;

    // RIFF header
    archivo.write((const uint8_t*)"RIFF", 4);
    archivo.write((const uint8_t*)&riffChunkSize, 4);
    archivo.write((const uint8_t*)"WAVE", 4);

    // fmt subchunk
    archivo.write((const uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16;
    archivo.write((const uint8_t*)&subchunk1Size, 4);
    uint16_t audioFormat = 1; // PCM
    archivo.write((const uint8_t*)&audioFormat, 2);
    archivo.write((const uint8_t*)&numChannels, 2);
    archivo.write((const uint8_t*)&sampleRate, 4);
    archivo.write((const uint8_t*)&byteRate, 4);
    archivo.write((const uint8_t*)&blockAlign, 2);
    archivo.write((const uint8_t*)&bitsPorMuestra, 2);

    // data subchunk
    archivo.write((const uint8_t*)"data", 4);
    archivo.write((const uint8_t*)&dataChunkSize, 4);
}

bool grabarAudioWAV(TipoGrabacionAudio tipo, const char *fechaHoraStr) {
    if (!i2sInicializado) {
        if (!inicializarAudioI2S()) {
            return false;
        }
    }

    if (!esSDDisponible()) {
        Serial.println("[ALERTA] MicroSD no disponible para almacenar bioacústica.");
        return false;
    }

    char rutaArchivo[96];
    if (tipo == TipoGrabacionAudio::NORMAL) {
        contadorAudiosNormal++;
        snprintf(rutaArchivo, sizeof(rutaArchivo), "/Audios_Normal/AudioNormal%u_%s.wav", contadorAudiosNormal, fechaHoraStr);
    } else {
        contadorAudiosPIR++;
        snprintf(rutaArchivo, sizeof(rutaArchivo), "/Audios_PIR/AudioPIR%u_%s.wav", contadorAudiosPIR, fechaHoraStr);
    }

    Serial.printf("[AUDIO] Iniciando ráfaga de 5s: %s\n", rutaArchivo);

    File archivoWav = SD.open(rutaArchivo, FILE_WRITE);
    if (!archivoWav) {
        Serial.println("[ERROR] No se pudo crear el archivo WAV en la MicroSD.");
        setLedErrorSD(true);
        return false;
    }

    // Dejar espacio de 44 bytes para la cabecera WAV final
    uint8_t cabeceraVacia[44] = {0};
    archivoWav.write(cabeceraVacia, 44);

    const size_t BUFFER_SIZE = 1024;
    uint8_t bufferDMA[BUFFER_SIZE];
    size_t bytesLeidos = 0;
    uint32_t muestrasTotales = 0;
    const uint32_t MUESTRAS_OBJETIVO = AUDIO_SAMPLE_RATE * AUDIO_DURATION_SEC;

    setLedKeepAlive(true);

    while (muestrasTotales < MUESTRAS_OBJETIVO) {
        esp_err_t err = i2s_read(I2S_PORT, bufferDMA, BUFFER_SIZE, &bytesLeidos, portMAX_DELAY);
        if (err == ESP_OK && bytesLeidos > 0) {
            archivoWav.write(bufferDMA, bytesLeidos);
            muestrasTotales += (bytesLeidos / 4); // 4 bytes por muestra en 32 bits
        }
    }

    // Regresar al byte 0 y escribir cabecera WAV real
    archivoWav.seek(0);
    escribirCabeceraWAV(archivoWav, muestrasTotales, AUDIO_SAMPLE_RATE, 32);
    archivoWav.flush();
    archivoWav.close();

    setLedKeepAlive(false);
    Serial.printf("[OK] Ráfaga de audio completada (%u muestras).\n", muestrasTotales);
    return true;
}

// Tarea concurrente FreeRTOS en Core 1
void tareaProcesamientoAudio(void *pvParameters) {
    (void)pvParameters;
    Serial.println("[FreeRTOS] Tarea de Bioacústica anclada a Core 1 lista (Compuerta: 5 min).");

    static unsigned long ultimoDisparoAudio = 0;
    static bool primerDisparoAudio = true;

    while (true) {
        // Espera no bloqueante hasta que un semáforo sea liberado (desde la ISR del PIR o temporizador)
        if (xSemaphoreTake(semaforoGrabacionAudio, portMAX_DELAY) == pdTRUE) {
            unsigned long ahora = millis();

            // Compuerta temporal: Cooldown de 5 minutos para evitar ráfagas descontroladas
            if (!primerDisparoAudio && (ahora - ultimoDisparoAudio < AUDIO_COOLDOWN_MS)) {
                unsigned long segundosRestantes = (AUDIO_COOLDOWN_MS - (ahora - ultimoDisparoAudio)) / 1000;
                Serial.printf("[AUDIO] Disparo ignorado por compuerta temporal (Cooldown activo: restan %lu s).\n", segundosRestantes);
                banderaPIRInterrupcion = false;
                continue;
            }

            primerDisparoAudio = false;
            char timestampBuffer[32];
            snprintf(timestampBuffer, sizeof(timestampBuffer), "%lu", ahora);

            bool disparoEsPIR = banderaPIRInterrupcion;
            banderaPIRInterrupcion = false;

            grabarAudioWAV(disparoEsPIR ? TipoGrabacionAudio::PIR : TipoGrabacionAudio::NORMAL, timestampBuffer);

            // Actualizar marca de tiempo tras la ráfaga para garantizar ventana de enfriamiento de 5 min
            ultimoDisparoAudio = millis();
        }
    }
}
