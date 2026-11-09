/**
 * @file main.cpp
 * @brief Firmware Principal InBeersa v2.2 (Escaneo Diagnóstico, Fallback AP y Arquitectura Desacoplada).
 * @author Daniel Felipe Duque Lizarralde & Kilby Semillero (UTP)
 * @version 2.2.1
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
volatile uint32_t contadorPIRAcumulado = 0;

static TelemetriaInBeersa telemetriaActual = {};

// Temporizadores no bloqueantes
static unsigned long ultimoMuestreoMeteorologico = 0;
static const unsigned long INTERVALO_MUESTREO_MS = 20000; // 20 segundos

static unsigned long ultimoAudioNormal = 0;
static const unsigned long INTERVALO_AUDIO_NORMAL_MS = 300000; // 5 minutos

static unsigned long ultimoLatido = 0;
static const unsigned long INTERVALO_LATIDO_MS = 1000; // 1 segundo (Heartbeat)

static unsigned long tiempoArranqueMillis = 0;

// =============================================================================
// RUTINA DE ADQUISICIÓN Y DISPACHO DE TELEMETRÍA
// =============================================================================
void ejecutarCicloTelemetria() {
    Serial.println("\n=======================================================");
    Serial.println("  INBEERSA KILBY - CICLO TELEMETRÍA BIOMETRIC CONTINUA ");
    Serial.println("=======================================================");

    strncpy(telemetriaActual.colmena, NOMBRE_COLMENA, sizeof(telemetriaActual.colmena) - 1);
    telemetriaActual.uptimeSeg = (millis() - tiempoArranqueMillis) / 1000;

    // 1. Obtener timestamp del RTC DS1307
    actualizarHoraRTC(telemetriaActual);

    // 2. Adquirir Sensor DHT22 Interno (Nido de Melipona)
    leerDHTInterno(telemetriaActual.tempInterna, telemetriaActual.humInterna);

    // 3. Adquirir Sensor DHT22 Externo (El driver lo gestiona si está habilitado en config_pins.h)
    bool extOk = leerDHTExterno(telemetriaActual.tempExterna, telemetriaActual.humExterna);
    if (extOk) {
        telemetriaActual.deltaTemp = telemetriaActual.tempInterna - telemetriaActual.tempExterna;
    } else {
        telemetriaActual.deltaTemp = 0.0f;
    }

    // 4. Adquirir Calidad de Aire MQ-135 (Retorna 0.0f limpio si está deshabilitado en config_pins.h)
    telemetriaActual.cAire = leerGasesMQ135();

    // 5. Cálculo biometeorológico de VPD en el Nido
    telemetriaActual.vpdInterno = calcularVPD(telemetriaActual.tempInterna, telemetriaActual.humInterna);

    // 6. Capturar estado de eventos de interrupción de hardware
    telemetriaActual.pirDetectado = banderaPIRInterrupcion ? 1 : 0;
    telemetriaActual.llovio = banderaLluviaInterrupcion ? 1 : 0;
    telemetriaActual.eventosPIRTotal = contadorPIRAcumulado;

    // Diagnóstico limpio en consola
    Serial.printf("[SENSOR] RTC Hora     : %s %s | Red: %s\n",
                  telemetriaActual.fecha, telemetriaActual.hora, obtenerIPLocal().c_str());
    Serial.printf("[SENSOR] Nido Interno : %.1f C | %.1f %% | VPD: %.2f kPa\n",
                  telemetriaActual.tempInterna, telemetriaActual.humInterna, telemetriaActual.vpdInterno);

    if (extOk) {
        Serial.printf("[SENSOR] Amb Exterior : %.1f C | %.1f %% | DeltaT: %.1f C\n",
                      telemetriaActual.tempExterna, telemetriaActual.humExterna, telemetriaActual.deltaTemp);
    }

    Serial.printf("[SENSOR] Actividad    : PIR=%d (Total: %u) | Lluvia=%d\n",
                  telemetriaActual.pirDetectado, telemetriaActual.eventosPIRTotal, telemetriaActual.llovio);

    // 7. Actualizar memoria caché para el Dashboard Web local (LAN / AP)
    atenderServidorWebLocal(telemetriaActual);

    // 8. Enviar a Ubidots Cloud vía MQTT (si hay enlace a internet)
    publicarUbidotsMQTT(telemetriaActual);

    // 9. Generar payload JSON compatible para guardar en MicroSD / LittleFS
    char bufferJSON[384];
    publicarHiveMQFallback(telemetriaActual, bufferJSON, sizeof(bufferJSON));
    guardarTelemetriaJSON(bufferJSON);

    // 10. Limpiar banderas de eventos rápidos para el nuevo periodo
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

    tiempoArranqueMillis = millis();

    Serial.println("\n*******************************************************");
    Serial.println("  INBEERSA V2.2: AUTO-FALLBACK AP + ESCANEO DIAGNÓSTICO");
    Serial.println("  Semillero Kilby UTP - Ecosistema TerraBit            ");
    Serial.println("*******************************************************");

    // 1. Inicializar Interfaz HMI y diagnóstico de LEDs
    inicializarHMI();
    secuenciaArranqueLeds();

    // 2. Primitivas FreeRTOS
    semaforoGrabacionAudio = xSemaphoreCreateBinary();
    mutexSPI = xSemaphoreCreateMutex();

    // 3. Inicializar Sensores y RTC
    inicializarSensores();

    // 4. Inicializar Periférico I2S y DMA de Audio (Core 1)
    inicializarAudioI2S();

    // 5. Inicializar Almacenamiento Masivo (Auto-detección MicroSD + LittleFS)
    inicializarAlmacenamiento();

    // 6. Inicializar Comunicaciones (Escaneo 2.4 GHz + WiFi STA "TerraBit" + Fallback AP)
    inicializarComunicaciones();

    // 7. Lanzar Tarea de Bioacústica en Core 1 dedicada
    xTaskCreatePinnedToCore(
        tareaProcesamientoAudio,
        "TaskAudioI2S",
        8192,
        NULL,
        2,              // Prioridad 2 para captura I2S sin pérdidas
        NULL,
        1               // Core 1 dedicado al bus I2S
    );

    // Primer muestreo de arranque
    ejecutarCicloTelemetria();

    Serial.println("[OK] Sistema InBeersa v2.2 listo y en ejecución continua.");
}

// =============================================================================
// BUCLE PRINCIPAL NO BLOQUEANTE
// =============================================================================
void loop() {
    unsigned long ahora = millis();

    // 1. Procesamiento continuo de red, peticiones Web HTTP y MQTT
    procesarComunicaciones();

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

    // Ceder CPU sin bloquear
    vTaskDelay(pdMS_TO_TICKS(10));
}
