/**
 * @file main.cpp
 * @brief Firmware Principal de la Estación InBeersa (Abejas Angelitas / Melipona eburnea).
 * @author Daniel Felipe Duque Lizarralde & Kilby Semillero (UTP)
 * @version 2.0.0 (C++17 Modular)
 */

#include <Arduino.h>
#include "config_pins.h"
#include "globals.h"
#include "hmi.h"
#include "sensors.h"
#include "audio_i2s.h"
#include "storage.h"
#include "comms.h"
#include <esp_task_wdt.h>

// =============================================================================
// DEFINICIÓN DE VARIABLES GLOBALES Y PRIMITIVAS FREERTOS
// =============================================================================
SemaphoreHandle_t semaforoGrabacionAudio = nullptr;
SemaphoreHandle_t mutexSPI = nullptr;
QueueHandle_t colaTelemetria = nullptr;

volatile bool banderaPIRInterrupcion = false;
volatile bool banderaLluviaInterrupcion = false;
volatile uint32_t contadorAudiosPIR = 0;
volatile uint32_t contadorAudiosNormal = 0;

static TelemetriaInBeersa telemetriaActual = {};

// Temporizadores no bloqueantes
static unsigned long ultimoMuestreoMeteorologico = 0;
static const unsigned long INTERVALO_MUESTREO_MS = 30000; // 30 segundos entre ciclos de telemetría

static unsigned long ultimoAudioNormal = 0;
static const unsigned long INTERVALO_AUDIO_NORMAL_MS = 300000; // 5 minutos entre capturas de audio de rutina

static unsigned long ultimoLatido = 0;
static const unsigned long INTERVALO_LATIDO_MS = 1000; // 1 segundo (Heartbeat)

// =============================================================================
// RUTINA DE ADQUISICIÓN Y DISPACHO DE TELEMETRÍA
// =============================================================================
void ejecutarCicloTelemetria() {
    Serial.println("\n=======================================================");
    Serial.println("  INBEERSA KILBY - CICLO DE TELEMETRÍA AGROMETEOROLÓGICA ");
    Serial.println("=======================================================");

    strncpy(telemetriaActual.colmena, NOMBRE_COLMENA, sizeof(telemetriaActual.colmena) - 1);

    // 1. Obtener timestamp del RTC
    actualizarHoraRTC(telemetriaActual);

    // 2. Adquirir sensores de temperatura y humedad
    leerDHTInterno(telemetriaActual.tempInterna, telemetriaActual.humInterna);
    leerDHTExterno(telemetriaActual.tempExterna, telemetriaActual.humExterna);

    // 3. Adquirir sensor de gases MQ-135 con EMA
    telemetriaActual.cAire = leerGasesMQ135();

    // 4. Capturar estado de banderas de eventos
    telemetriaActual.pirDetectado = banderaPIRInterrupcion ? 1 : 0;
    telemetriaActual.llovio = banderaLluviaInterrupcion ? 1 : 0;

    // Imprimir diagnóstico por consola
    Serial.printf("[SENSOR] Fecha/Hora : %s %s\n", telemetriaActual.fecha, telemetriaActual.hora);
    Serial.printf("[SENSOR] DHT Nido   : %.1f C | %.1f %%\n", telemetriaActual.tempInterna, telemetriaActual.humInterna);
    Serial.printf("[SENSOR] DHT Amb    : %.1f C | %.1f %%\n", telemetriaActual.tempExterna, telemetriaActual.humExterna);
    Serial.printf("[SENSOR] MQ-135 Gas : %.1f (EMA)\n", telemetriaActual.cAire);
    Serial.printf("[SENSOR] Eventos    : PIR=%d | Lluvia=%d\n", telemetriaActual.pirDetectado, telemetriaActual.llovio);

    // 5. Serializar a JSON
    char bufferJSON[384];
    publicarTelemetriaMQTT(telemetriaActual, bufferJSON, sizeof(bufferJSON));

    // 6. Guardar en memoria MicroSD (o LittleFS de respaldo)
    guardarTelemetriaJSON(bufferJSON);

    // 7. Limpiar banderas de eventos para el próximo periodo
    banderaPIRInterrupcion = false;
    banderaLluviaInterrupcion = false;

    Serial.println("-------------------------------------------------------\n");
}

// =============================================================================
// SETUP DEL SISTEMA
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n*******************************************************");
    Serial.println("  INICIALIZANDO ESTACIÓN INBEERSA V2.0 (C++17 MODULAR)");
    Serial.println("  Semillero Kilby UTP - Melipona eburnea Bio-IoT");
    Serial.println("*******************************************************");

    // 1. Inicializar Interfaz HMI y diagnóstico
    inicializarHMI();
    secuenciaArranqueLeds();

    // 2. Crear primitivas de sincronización FreeRTOS
    semaforoGrabacionAudio = xSemaphoreCreateBinary();
    mutexSPI = xSemaphoreCreateMutex();

    // 3. Inicializar Sensores y RTC
    inicializarSensores();

    // 4. Inicializar Periférico I2S y DMA de Audio
    inicializarAudioI2S();

    // 5. Inicializar Almacenamiento Masivo (MicroSD + LittleFS)
    inicializarAlmacenamiento();

    // 6. Inicializar Comunicaciones WiFi y MQTT
    inicializarComunicaciones();

    // 7. Lanzar Tarea de Bioacústica en Core 1 dedicada
    xTaskCreatePinnedToCore(
        tareaProcesamientoAudio,
        "TaskAudioI2S",
        8192,
        NULL,
        2,              // Prioridad alta para no perder muestras DMA
        NULL,
        1               // Core 1 (Core 0 dedicado a WiFi/TCP/IP)
    );

    // Primer muestreo de arranque
    ejecutarCicloTelemetria();

    Serial.println("[OK] Sistema operativo y tareas en ejecución.");
}

// =============================================================================
// BUCLE PRINCIPAL NO BLOQUEANTE
// =============================================================================
void loop() {
    unsigned long ahora = millis();

    // 1. Procesamiento de red y MQTT sin bloquear la CPU
    procesarBucleMQTT();

    // 2. Parpadeo del LED de vida (Heartbeat)
    if (ahora - ultimoLatido >= INTERVALO_LATIDO_MS) {
        ultimoLatido = ahora;
        toggleKeepAlive();
    }

    // 3. Ciclo periódico de adquisición meteorológica
    if (ahora - ultimoMuestreoMeteorologico >= INTERVALO_MUESTREO_MS) {
        ultimoMuestreoMeteorologico = ahora;
        ejecutarCicloTelemetria();
    }

    // 4. Disparo de grabación de audio de rutina ("Audio Normal")
    if (ahora - ultimoAudioNormal >= INTERVALO_AUDIO_NORMAL_MS) {
        ultimoAudioNormal = ahora;
        if (semaforoGrabacionAudio != nullptr) {
            xSemaphoreGive(semaforoGrabacionAudio);
        }
    }

    // Ceder tiempo a tareas de menor prioridad
    vTaskDelay(pdMS_TO_TICKS(10));
}
