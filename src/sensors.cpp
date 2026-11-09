/**
 * @file sensors.cpp
 * @brief Implementación de drivers de adquisición ambiental, gases, RTC y psicrometría (VPD).
 */

#include "sensors.h"
#include "hmi.h"
#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include <math.h>

// Instancias de sensores
static DHT dhtInterno(PIN_DHT_INTERNO, DHT22);
static DHT dhtExterno(PIN_DHT_EXTERNO, DHT22);
static RTC_DS1307 rtc;

// Variable de estado para el filtro EMA de gas
static float ema_gas = -1.0f;
static const float ALPHA_EMA_GAS = 0.2f;

// Variables de debounce para interrupciones de hardware
static volatile unsigned long ultimaInterrupcionPIR = 0;
static volatile unsigned long ultimaInterrupcionLluvia = 0;
static const unsigned long DEBOUNCE_PIR_MS = 2500;      // 2.5s entre detecciones de abejas
static const unsigned long DEBOUNCE_LLUVIA_MS = 1000;   // 1s para gotas de lluvia

// =============================================================================
// ISR DE HARDWARE (ULTRA LIGERAS)
// =============================================================================
void IRAM_ATTR isrPIR() {
    unsigned long tiempoActual = millis();
    if (tiempoActual - ultimaInterrupcionPIR > DEBOUNCE_PIR_MS) {
        ultimaInterrupcionPIR = tiempoActual;
        banderaPIRInterrupcion = true;
        contadorPIRAcumulado++;
        
        // Despertar tarea de bioacústica mediante semáforo de FreeRTOS
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        if (semaforoGrabacionAudio != nullptr) {
            xSemaphoreGiveFromISR(semaforoGrabacionAudio, &higherPriorityTaskWoken);
        }
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

void IRAM_ATTR isrLluvia() {
    unsigned long tiempoActual = millis();
    if (tiempoActual - ultimaInterrupcionLluvia > DEBOUNCE_LLUVIA_MS) {
        ultimaInterrupcionLluvia = tiempoActual;
        banderaLluviaInterrupcion = true;
    }
}

// =============================================================================
// RUTINAS DE INICIALIZACIÓN Y ADQUISICIÓN
// =============================================================================
bool inicializarSensores() {
    bool estadoGlobal = true;

    // 1. Bus I2C para RTC DS1307 (Pines confirmados SDA=5, SCL=4)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeout(1000);

    if (!rtc.begin()) {
        Serial.println("[ERROR] RTC DS1307 no responde en bus I2C (0x68).");
        setLedErrorRTC(true);
        estadoGlobal = false;
    } else {
        if (!rtc.isrunning()) {
            Serial.println("[ALERTA] RTC detenido. Sincronizando con tiempo de compilación.");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        Serial.println("[OK] RTC DS1307 en línea y operativo.");
        setLedErrorRTC(false);
    }

    // 2. Sensores DHT22
    if (HABILITAR_DHT_INTERNO) {
        dhtInterno.begin();
    }
    if (HABILITAR_DHT_EXTERNO) {
        dhtExterno.begin();
    }

    // 3. ADC para MQ-135
    if (HABILITAR_MQ135) {
        analogReadResolution(12);
        pinMode(PIN_MQ135_GAS, INPUT);
    }

    // 4. Configuración de Interrupciones de Hardware
    configurarInterrupcionesHardware();

    // 5. Limpieza de transitorios iniciales de la fuente de alimentación
    delay(100);
    banderaLluviaInterrupcion = false;
    banderaPIRInterrupcion = false;

    return estadoGlobal;
}

void configurarInterrupcionesHardware() {
    if (HABILITAR_PIR) {
        pinMode(PIN_PIR_MOVIMIENTO, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_PIR_MOVIMIENTO), isrPIR, RISING);
    }

    if (HABILITAR_LLUVIA) {
        pinMode(PIN_SENSOR_LLUVIA, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PIN_SENSOR_LLUVIA), isrLluvia, FALLING);
    }

    Serial.println("[OK] Interrupciones PIR y Lluvia armadas.");
}

bool leerDHTInterno(float &temp, float &hum) {
    if (!HABILITAR_DHT_INTERNO) {
        temp = 0.0f;
        hum = 0.0f;
        return false;
    }

    float t = dhtInterno.readTemperature();
    float h = dhtInterno.readHumidity();

    if (isnan(t) || isnan(h)) {
        Serial.println("[ERROR] DHT Interno no responde.");
        setLedErrorDHTI(true);
        temp = -200.0f;
        hum = -200.0f;
        return false;
    }

    temp = t;
    hum = h;
    setLedErrorDHTI(false);
    return true;
}

bool leerDHTExterno(float &temp, float &hum) {
    if (!HABILITAR_DHT_EXTERNO) {
        temp = 0.0f;
        hum = 0.0f;
        setLedErrorDHTE(false);
        return false;
    }

    float t = dhtExterno.readTemperature();
    float h = dhtExterno.readHumidity();

    if (isnan(t) || isnan(h)) {
        Serial.println("[ERROR] DHT Externo no responde.");
        setLedErrorDHTE(true);
        temp = -200.0f;
        hum = -200.0f;
        return false;
    }

    temp = t;
    hum = h;
    setLedErrorDHTE(false);
    return true;
}

float leerGasesMQ135() {
    if (!HABILITAR_MQ135) {
        return 0.0f;
    }

    uint32_t acumulador = 0;
    for (int i = 0; i < 8; i++) {
        acumulador += analogRead(PIN_MQ135_GAS);
        delayMicroseconds(250);
    }
    float rawPromedio = static_cast<float>(acumulador) / 8.0f;

    if (ema_gas < 0.0f) {
        ema_gas = rawPromedio;
    } else {
        ema_gas = (ALPHA_EMA_GAS * rawPromedio) + ((1.0f - ALPHA_EMA_GAS) * ema_gas);
    }

    return ema_gas;
}

float calcularVPD(float temperatura, float humedadRelativa) {
    if (temperatura < -50.0f || humedadRelativa < 0.0f) {
        return -1.0f;
    }
    float svp = 0.61078f * expf((17.27f * temperatura) / (temperatura + 237.3f));
    float vpd = svp * (1.0f - (humedadRelativa / 100.0f));
    return (vpd < 0.0f) ? 0.0f : vpd;
}

bool actualizarHoraRTC(TelemetriaInBeersa &datos) {
    if (!rtc.isrunning()) {
        snprintf(datos.fecha, sizeof(datos.fecha), "Error/Error/Error");
        snprintf(datos.hora, sizeof(datos.hora), "Error:Error:Error");
        datos.rtcValido = false;
        setLedErrorRTC(true);
        return false;
    }

    DateTime now = rtc.now();
    snprintf(datos.fecha, sizeof(datos.fecha), "%02d/%02d/%04d", now.day(), now.month(), now.year());
    snprintf(datos.hora, sizeof(datos.hora), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    datos.rtcValido = true;
    setLedErrorRTC(false);
    return true;
}

bool sincronizarRTCPorNTP() {
    Serial.println("[NTP] Sincronizando RTC con pool.ntp.org...");
    configTime(-5 * 3600, 0, "pool.ntp.org", "time.google.com");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        Serial.println("[ALERTA] Servidor NTP no respondió. Se mantiene hora del RTC.");
        return false;
    }

    rtc.adjust(DateTime(timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1,
                        timeinfo.tm_mday,
                        timeinfo.tm_hour,
                        timeinfo.tm_min,
                        timeinfo.tm_sec));

    Serial.println("[OK] RTC sincronizado con servidor atómico.");
    return true;
}
