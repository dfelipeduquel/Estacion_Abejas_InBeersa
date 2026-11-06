/**
 * @file config_pins.h
 * @brief Mapeo de pines de hardware y configuración eléctrica para la estación InBeersa.
 * @note Conciliación entre código legacy y relevamiento de continuidad físico en PCB (Kilby UTP).
 */

#ifndef CONFIG_PINS_H
#define CONFIG_PINS_H

#include <Arduino.h>

// =============================================================================
// SENSORES METEOROLÓGICOS Y AMBIENTALES
// =============================================================================
#define PIN_DHT_INTERNO       2    // J5 - Sensor DHT22 interior de colmena (Nido)
#define PIN_DHT_EXTERNO      23    // J6 - Sensor DHT22 exterior (Ambiente)
#define PIN_MQ135_GAS        34    // J3 - Sensor Gases / Calidad de Aire (ADC1_CH6 analógico)
#define PIN_SENSOR_LLUVIA    35    // J8 - Sensor de lluvia por interrupción (Falling)

// =============================================================================
// BIOACÚSTICA - MICRÓFONO DIGITAL I2S (INMP441)
// =============================================================================
#define PIN_I2S_SCK          18    // BCLK / Reloj serie de bits
#define PIN_I2S_WS           33    // LRCLK / Word Select / Selector de canal
#define PIN_I2S_SD           27    // DOUT del micro hacia DIN del ESP32

// Parámetros de Adquisición Bioacústica
#define AUDIO_SAMPLE_RATE     16000  // 16 kHz
#define AUDIO_DURATION_SEC    5      // Ráfagas de 5 segundos de grabación
#define AUDIO_DMA_BUF_COUNT   8      // Cantidad de descriptores DMA
#define AUDIO_DMA_BUF_LEN     1024   // Muestras por descriptor DMA

// =============================================================================
// DETECCIÓN BIOLÓGICA - PIR (MOVIMIENTO EN PIQUERA)
// =============================================================================
#define PIN_PIR_MOVIMIENTO   14    // J1 - Confirmado por continuidad en PCB

// =============================================================================
// RELOJ DE TIEMPO REAL (RTC DS1307 / DS3231) POR I2C
// =============================================================================
#define PIN_I2C_SDA           5    // Bus I2C de datos (SDA)
#define PIN_I2C_SCL           4    // Bus I2C de reloj (SCL)
#define RTC_I2C_ADDR       0x68    // Dirección esclava estándar RTC DS1307

// =============================================================================
// ALMACENAMIENTO - TARJETA MICROSD (BUS SPI)
// =============================================================================
#define PIN_SD_MOSI          17    // MOSI SPI
#define PIN_SD_MISO          16    // MISO SPI
#define PIN_SD_SCK           12    // SCK SPI
#define PIN_SD_CS            13    // CS SPI por defecto

// =============================================================================
// INTERFAZ DE DIAGNÓSTICO (HMI) - LEDS DE ESTADO (Lógica Inversa)
// =============================================================================
#define PIN_LED_KEEP_ALIVE   21    // LED de latido del sistema (Heartbeat)
#define PIN_LED_RTC_MAL      19    // LED indicador de fallo en RTC
#define PIN_LED_DHTI_MAL     22    // LED indicador de fallo en DHT Interno
#define PIN_LED_DHTE_MAL     25    // LED indicador de fallo en DHT Externo
#define PIN_LED_SD_MAL       32    // LED indicador de fallo en MicroSD

// =============================================================================
// PARÁMETROS DE RED WIFI (RED LOCAL DEL USUARIO)
// =============================================================================
#define DEFAULT_WIFI_SSID     "TerraBit"
#define DEFAULT_WIFI_PASS     "TU_CLAVE_WIFI"

// MODO DUAL DE CONECTIVIDAD:
// 1 = Publicar a Ubidots Cloud
// 2 = Publicar a HiveMQ Broker Público (Tesis / Receptor Audio)
// 3 = Servidor Web Local Autónomo en LAN (IP fija o mDNS: inbeersa.local)
#define MODO_TELEMETRIA_UBIDOTS 1

// Credenciales Ubidots IoT (Industrial / STEM)
#define UBIDOTS_BROKER        "industrial.api.ubidots.com"
#define UBIDOTS_PORT          1883
#define UBIDOTS_TOKEN         "BBUS-TU_TOKEN_DE_UBIDOTS"
#define UBIDOTS_DEVICE_LABEL  "inbeersa-nodo01"

// HiveMQ Broker Legacy (Respaldo)
#define HIVEMQ_BROKER         "broker.hivemq.com"
#define HIVEMQ_PORT           1883
#define HIVEMQ_TOPIC          "ProyectoMeliponas/topic"

#define NOMBRE_COLMENA        "Colmena Villagarzon 2/1"

#endif // CONFIG_PINS_H
