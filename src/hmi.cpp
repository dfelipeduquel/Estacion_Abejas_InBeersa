/**
 * @file hmi.cpp
 * @brief Implementación del subsistema de diagnóstico por LEDs.
 * @note La PCB de InBeersa utiliza lógica inversa para los LEDs de falla (LOW = Encendido).
 */

#include "hmi.h"

void inicializarHMI() {
    pinMode(PIN_LED_KEEP_ALIVE, OUTPUT);
    pinMode(PIN_LED_RTC_MAL, OUTPUT);
    pinMode(PIN_LED_DHTI_MAL, OUTPUT);
    pinMode(PIN_LED_DHTE_MAL, OUTPUT);
    pinMode(PIN_LED_SD_MAL, OUTPUT);

    // Apagar todos por defecto (Lógica inversa: HIGH = Apagado para fallas)
    digitalWrite(PIN_LED_KEEP_ALIVE, LOW);
    digitalWrite(PIN_LED_RTC_MAL, HIGH);
    digitalWrite(PIN_LED_DHTI_MAL, HIGH);
    digitalWrite(PIN_LED_DHTE_MAL, HIGH);
    digitalWrite(PIN_LED_SD_MAL, HIGH);
}

void toggleKeepAlive() {
    static bool estado = false;
    estado = !estado;
    digitalWrite(PIN_LED_KEEP_ALIVE, estado ? HIGH : LOW);
}

void setLedKeepAlive(bool estado) {
    digitalWrite(PIN_LED_KEEP_ALIVE, estado ? HIGH : LOW);
}

void setLedErrorRTC(bool error) {
    // Lógica inversa: error (true) -> LOW (enciende LED de fallo)
    digitalWrite(PIN_LED_RTC_MAL, error ? LOW : HIGH);
}

void setLedErrorDHTI(bool error) {
    digitalWrite(PIN_LED_DHTI_MAL, error ? LOW : HIGH);
}

void setLedErrorDHTE(bool error) {
    digitalWrite(PIN_LED_DHTE_MAL, error ? LOW : HIGH);
}

void setLedErrorSD(bool error) {
    digitalWrite(PIN_LED_SD_MAL, error ? LOW : HIGH);
}

void secuenciaArranqueLeds() {
    // Secuencia visual de autodiagnóstico no bloqueante en producción
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED_KEEP_ALIVE, HIGH);
        digitalWrite(PIN_LED_RTC_MAL, LOW);
        digitalWrite(PIN_LED_DHTI_MAL, LOW);
        digitalWrite(PIN_LED_DHTE_MAL, LOW);
        digitalWrite(PIN_LED_SD_MAL, LOW);
        delay(150);
        digitalWrite(PIN_LED_KEEP_ALIVE, LOW);
        digitalWrite(PIN_LED_RTC_MAL, HIGH);
        digitalWrite(PIN_LED_DHTI_MAL, HIGH);
        digitalWrite(PIN_LED_DHTE_MAL, HIGH);
        digitalWrite(PIN_LED_SD_MAL, HIGH);
        delay(150);
    }
}
