/**
 * @file comms.cpp
 * @brief Implementación de conectividad WiFi y publicación MQTT no bloqueante.
 */

#include "comms.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

static unsigned long ultimoIntentoWiFi = 0;
static const unsigned long REINTENTO_WIFI_MS = 15000; // 15 segundos entre intentos

static unsigned long ultimoIntentoMQTT = 0;
static const unsigned long REINTENTO_MQTT_MS = 10000; // 10 segundos entre intentos de broker

void inicializarComunicaciones() {
    Serial.println("[COMMS] Inicializando subsistema WiFi STA...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setBufferSize(512);
}

void verificarConectividadWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    unsigned long ahora = millis();
    if (ahora - ultimoIntentoWiFi >= REINTENTO_WIFI_MS) {
        ultimoIntentoWiFi = ahora;
        Serial.println("[WiFi] Reintentando acople a red...");
        WiFi.disconnect();
        WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
    }
}

static bool reconectarMQTT() {
    if (mqttClient.connected()) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    unsigned long ahora = millis();
    if (ahora - ultimoIntentoMQTT >= REINTENTO_MQTT_MS) {
        ultimoIntentoMQTT = ahora;
        Serial.print("[MQTT] Conectando a HiveMQ Broker...");
        if (mqttClient.connect(MQTT_CLIENT_ID)) {
            Serial.println(" Conectado!");
            return true;
        } else {
            Serial.printf(" Falló (rc=%d)\n", mqttClient.state());
            return false;
        }
    }
    return false;
}

void procesarBucleMQTT() {
    verificarConectividadWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            reconectarMQTT();
        } else {
            mqttClient.loop();
        }
    }
}

bool publicarTelemetriaMQTT(const TelemetriaInBeersa &datos, char *jsonBufferOut, size_t bufferSize) {
    // 1. Serializar el documento JSON compatible con la estructura de la maestría
    StaticJsonDocument<384> doc;
    doc["colmena"]  = datos.colmena;
    doc["Fecha"]    = datos.fecha;
    doc["Hora"]     = datos.hora;
    doc["TempI"]    = round(datos.tempInterna * 10.0f) / 10.0f;
    doc["HumedadI"] = round(datos.humInterna * 10.0f) / 10.0f;
    doc["TempE"]    = round(datos.tempExterna * 10.0f) / 10.0f;
    doc["HumedadE"] = round(datos.humExterna * 10.0f) / 10.0f;
    doc["CAire"]    = round(datos.cAire * 10.0f) / 10.0f;
    doc["PIR"]      = datos.pirDetectado;
    doc["Lluvia"]   = datos.llovio;

    serializeJson(doc, jsonBufferOut, bufferSize);

    // 2. Publicar por MQTT si hay conexión
    if (mqttClient.connected()) {
        bool res = mqttClient.publish(MQTT_TOPIC_TELEMETRIA, jsonBufferOut);
        if (res) {
            Serial.printf("[MQTT] Publicado con éxito: %s\n", jsonBufferOut);
            return true;
        } else {
            Serial.println("[ERROR] Fallo al publicar paquete en tópico MQTT.");
            return false;
        }
    } else {
        Serial.println("[ALERTA] MQTT desconectado. El paquete se almacena localmente.");
        return false;
    }
}
