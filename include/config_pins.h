/**
 * @file config_pins.h
 * @brief Mapeo de pines de hardware, flags de acondicionamiento y conectividad para InBeersa.
 */

#ifndef CONFIG_PINS_H
#define CONFIG_PINS_H

#include <Arduino.h>

// =============================================================================
// FLAGS DE ACONDICIONAMIENTO DE SENSORES CONECTADOS (HARDWARE REAL)
// =============================================================================
#define HABILITAR_DHT_INTERNO  1   // 1 = Conectado y Operativo
#define HABILITAR_DHT_EXTERNO  0   // 0 = Desactivado (no conectado físicamente)
#define HABILITAR_MQ135        0   // 0 = Desactivado (no conectado físicamente)
#define HABILITAR_PIR          1   // 1 = Sensor PIR Movimiento en Piquera
#define HABILITAR_LLUVIA       1   // 1 = Sensor de Lluvia por Interrupción

// =============================================================================
// SENSORES METEOROLÓGICOS Y AMBIENTALES
// =============================================================================
#define PIN_DHT_INTERNO       2    // J5 - Sensor DHT22 interior de colmena (Nido)
#define PIN_DHT_EXTERNO      23    // J6 - Sensor DHT22 exterior (Ambiente)
#define PIN_MQ135_GAS        34    // J3 - Sensor Gases / Calidad de Aire (ADC1_CH6)
#define PIN_SENSOR_LLUVIA    35    // J8 - Sensor de lluvia por interrupción (Falling)

// =============================================================================
// BIOACÚSTICA - MICRÓFONO DIGITAL I2S (INMP441)
// =============================================================================
#define PIN_I2S_SCK          18    // BCLK
#define PIN_I2S_WS           32    // LRCLK / Word Select (Corregido v2.3)
#define PIN_I2S_SD           26    // DOUT / Data In (Corregido v2.3)

#define AUDIO_SAMPLE_RATE     16000  // 16 kHz
#define AUDIO_DURATION_SEC    5      // 5 segundos de ráfaga
#define AUDIO_DMA_BUF_COUNT   8
#define AUDIO_DMA_BUF_LEN     1024
#define AUDIO_COOLDOWN_MS     300000 // 5 minutos de compuerta temporal (dead-time)

// =============================================================================
// DETECCIÓN BIOLÓGICA - PIR (MOVIMIENTO EN PIQUERA)
// =============================================================================
#define PIN_PIR_MOVIMIENTO   27    // J1 (Corregido v2.3: antes 14 colisionaba con SCK SPI)

// =============================================================================
// RELOJ DE TIEMPO REAL (RTC DS1307) POR I2C
// =============================================================================
#define PIN_I2C_SDA           5    // SDA I2C
#define PIN_I2C_SCL           4    // SCL I2C
#define RTC_I2C_ADDR       0x68

// =============================================================================
// ALMACENAMIENTO - TARJETA MICROSD (BUS SPI)
// Mapeo Primario (Probado en código legacy MicroPython funcional en campo):
//   SCK=14, MOSI=13, MISO=16, CS=17
// Mapeo Secundario de Contingencia:
//   SCK=12, MOSI=17, MISO=16, CS=13
// =============================================================================
#define PIN_SD_SCK           14    // SCK SPI (Legacy funcional)
#define PIN_SD_MOSI          13    // MOSI SPI
#define PIN_SD_MISO          16    // MISO SPI
#define PIN_SD_CS            17    // CS SPI

#define PIN_SD_SCK_ALT       12    // SCK alternativo
#define PIN_SD_MOSI_ALT      17    // MOSI alternativo
#define PIN_SD_CS_ALT        13    // CS alternativo

// =============================================================================
// INTERFAZ DE DIAGNÓSTICO (HMI) - LEDS DE ESTADO (Lógica Inversa: LOW = ON)
// =============================================================================
#define PIN_LED_KEEP_ALIVE   21    // LED Heartbeat
#define PIN_LED_RTC_MAL      19    // LED fallo RTC
#define PIN_LED_DHTI_MAL     22    // LED fallo DHT Interno
#define PIN_LED_DHTE_MAL     25    // LED fallo DHT Externo
#define PIN_LED_SD_MAL       33    // LED fallo MicroSD (Reubicado de 32 a 33 para evitar cruce con WS I2S)

// =============================================================================
// PARÁMETROS DE RED WIFI (RED LOCAL "TerraBit") & MODO AUXILIAR
// =============================================================================
#define DEFAULT_WIFI_SSID     "TerraBit"
#define DEFAULT_WIFI_PASS     "TU_CLAVE_WIFI"
#define DEFAULT_WIFI_POWER    WIFI_POWER_17dBm  // 17 dBm (~50 mW): Atenúa picos de corriente para evitar brownout en MicroSD

// Access Point de Emergencia (Fallback si el router no autentica)
#define FALLBACK_AP_SSID      "InBeersa-Auxiliar"
#define FALLBACK_AP_PASS      "solarpunk2026"

// Ubidots Cloud
#define UBIDOTS_BROKER        "industrial.api.ubidots.com"
#define UBIDOTS_PORT          1883
#define UBIDOTS_TOKEN         "BBUS-TU_TOKEN_DE_UBIDOTS"
#define UBIDOTS_DEVICE_LABEL  "inbeersa-nodo01"

// HiveMQ Broker Legacy
#define HIVEMQ_BROKER         "broker.hivemq.com"
#define HIVEMQ_PORT           1883
#define HIVEMQ_TOPIC          "ProyectoMeliponas/topic"

#define NOMBRE_COLMENA        "Colmena Villagarzon 2/1"

#endif // CONFIG_PINS_H
