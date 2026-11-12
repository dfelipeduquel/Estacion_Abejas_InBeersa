/**
 * @file globals.h
 * @brief Tipos de datos globales, estructura de telemetría, VPD y concurrencia.
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

/**
 * @brief Estructura de telemetría completa para la estación InBeersa.
 * Incluye variables biometeorológicas avanzadas (VPD, Deltas térmicos).
 */
struct TelemetriaInBeersa {
    char colmena[32];      // Nombre del nodo / colmena
    char fecha[16];        // Formato DD/MM/AAAA
    char hora[16];         // Formato HH:MM:SS
    float tempInterna;     // Temperatura nido en °C (DHT22 Interior)
    float humInterna;      // Humedad relativa nido en % (DHT22 Interior)
    float tempExterna;     // Temperatura ambiente en °C (DHT22 Exterior)
    float humExterna;      // Humedad relativa ambiente en % (DHT22 Exterior)
    float cAire;           // Calidad del aire / CO2 (MQ-135 filtrado por EMA)
    float vpdInterno;      // Déficit de Presión de Vapor en Nido (kPa)
    float deltaTemp;       // Capacidad de termorregulación: TempInterna - TempExterna (°C)
    uint8_t pirDetectado;  // 1 = movimiento detectado en piquera, 0 = reposo
    uint8_t llovio;        // 1 = gotas detectadas en el periodo, 0 = seco
    uint32_t eventosPIRTotal; // Contador acumulado de eventos en piquera
    bool rtcValido;        // Estado de sincronización del RTC
    bool sdValida;         // Estado de la memoria MicroSD
    bool wifiValido;       // Estado del enlace WiFi
    bool mqttValido;       // Estado del enlace al broker MQTT
    uint32_t uptimeSeg;    // Segundos continuos en línea
};

// =============================================================================
// PRIMITIVAS DE CONCURRENCIA FREERTOS (SINCRONIZACIÓN ASÍNCRONA)
// =============================================================================
extern SemaphoreHandle_t semaforoGrabacionAudio; // Despierta la tarea de audio desde ISR o timer
extern SemaphoreHandle_t mutexSPI;              // Protege el bus SPI
extern QueueHandle_t colaTelemetria;            // Cola de paquetes de telemetría

// Banderas atómicas para eventos rápidos desde interrupciones
extern volatile bool banderaPIRInterrupcion;
extern volatile bool banderaLluviaInterrupcion;
extern volatile bool banderaAudioManualWeb;
extern volatile uint32_t contadorAudiosPIR;
extern volatile uint32_t contadorAudiosNormal;
extern volatile uint32_t contadorPIRAcumulado;

#endif // GLOBALS_H
