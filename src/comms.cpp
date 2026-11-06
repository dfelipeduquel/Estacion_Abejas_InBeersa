/**
 * @file comms.cpp
 * @brief Implementación de conectividad WiFi persistente, Ubidots Cloud y Dashboard Web LAN.
 */

#include "comms.h"
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);
static WebServer servidorWeb(80);

static unsigned long ultimoIntentoWiFi = 0;
static const unsigned long REINTENTO_WIFI_MS = 10000;

static unsigned long ultimoIntentoMQTT = 0;
static const unsigned long REINTENTO_MQTT_MS = 8000;

static TelemetriaInBeersa ultimaMedicionCache = {};

// =============================================================================
// DASHBOARD WEB LOCAL (LAN DIRECTA EN NAVEGADOR)
// =============================================================================
static const char HTML_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>InBeersa - Monitor Meliponas</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0d1117; color: #c9d1d9; margin: 0; padding: 20px; }
        .container { max-width: 600px; margin: 0 auto; }
        .card { background: #161b22; border: 1px solid #30363d; border-radius: 12px; padding: 20px; margin-bottom: 16px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }
        h1 { color: #f0883e; margin: 0 0 8px 0; font-size: 22px; display: flex; align-items: center; gap: 8px; }
        .subtitle { color: #8b949e; font-size: 13px; margin-bottom: 20px; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        .metric { background: #0d1117; padding: 14px; border-radius: 8px; border-left: 4px solid #f0883e; }
        .metric-title { font-size: 12px; color: #8b949e; text-transform: uppercase; }
        .metric-value { font-size: 22px; font-weight: bold; color: #58a6ff; margin-top: 4px; }
        .metric-unit { font-size: 13px; color: #8b949e; }
        .status-badge { display: inline-block; padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: bold; background: #238636; color: #fff; }
        .footer { text-align: center; font-size: 11px; color: #8b949e; margin-top: 24px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>🐝 InBeersa - Nodo Melipona</h1>
            <div class="subtitle">Semillero Kilby UTP | Monitoreo Bio-IoT en Tiempo Real</div>
            <div>Estado Estación: <span class="status-badge">EN LÍNEA (24/7)</span></div>
            <p style="font-size:12px; color:#8b949e;">Fecha / Hora: <strong>%FECHA% %HORA%</strong></p>
            
            <div class="grid">
                <div class="metric" style="border-color:#58a6ff;">
                    <div class="metric-title">Nido Interno</div>
                    <div class="metric-value">%TEMPI% <span class="metric-unit">°C</span></div>
                    <div style="font-size:13px; color:#8b949e;">HR: %HUMI% %%</div>
                </div>
                <div class="metric" style="border-color:#7ee787;">
                    <div class="metric-title">Ambiente Exterior</div>
                    <div class="metric-value">%TEMPE% <span class="metric-unit">°C</span></div>
                    <div style="font-size:13px; color:#8b949e;">HR: %HUME% %%</div>
                </div>
                <div class="metric" style="border-color:#f0883e;">
                    <div class="metric-title">VPD en Nido</div>
                    <div class="metric-value">%VPD% <span class="metric-unit">kPa</span></div>
                    <div style="font-size:13px; color:#8b949e;">ΔT: %DELTAT% °C</div>
                </div>
                <div class="metric" style="border-color:#d29922;">
                    <div class="metric-title">Calidad Aire (Gas)</div>
                    <div class="metric-value">%GAS% <span class="metric-unit">ADC</span></div>
                    <div style="font-size:13px; color:#8b949e;">MQ-135 (EMA)</div>
                </div>
                <div class="metric" style="border-color:#bc8cff;">
                    <div class="metric-title">Movimiento Piquera</div>
                    <div class="metric-value">%PIR% <span class="metric-unit">eventos</span></div>
                    <div style="font-size:13px; color:#8b949e;">PIR: %ESTADOPIR%</div>
                </div>
                <div class="metric" style="border-color:#ff7b72;">
                    <div class="metric-title">Sensor de Lluvia</div>
                    <div class="metric-value">%LLUVIA%</div>
                    <div style="font-size:13px; color:#8b949e;">Uptime: %UPTIME%s</div>
                </div>
            </div>
        </div>
        <div class="footer">TerraBit Ecosystem & Semillero Kilby UTP | 2026</div>
    </div>
</body>
</html>
)rawliteral";

static String generarDashboardHTML() {
    String html = FPSTR(HTML_DASHBOARD);
    html.replace("%FECHA%", ultimaMedicionCache.fecha);
    html.replace("%HORA%", ultimaMedicionCache.hora);
    html.replace("%TEMPI%", String(ultimaMedicionCache.tempInterna, 1));
    html.replace("%HUMI%", String(ultimaMedicionCache.humInterna, 1));
    html.replace("%TEMPE%", String(ultimaMedicionCache.tempExterna, 1));
    html.replace("%HUME%", String(ultimaMedicionCache.humExterna, 1));
    html.replace("%VPD%", String(ultimaMedicionCache.vpdInterno, 2));
    html.replace("%DELTAT%", String(ultimaMedicionCache.deltaTemp, 1));
    html.replace("%GAS%", String(ultimaMedicionCache.cAire, 0));
    html.replace("%PIR%", String(ultimaMedicionCache.eventosPIRTotal));
    html.replace("%ESTADOPIR%", ultimaMedicionCache.pirDetectado ? "ACTIVO" : "REPOSO");
    html.replace("%LLUVIA%", ultimaMedicionCache.llovio ? "Lloviendo 🌧️" : "Seco ☀️");
    html.replace("%UPTIME%", String(ultimaMedicionCache.uptimeSeg));
    return html;
}

// =============================================================================
// INICIALIZACIÓN Y GESTIÓN DE CONECTIVIDAD
// =============================================================================
void inicializarComunicaciones() {
    Serial.printf("[WiFi] Conectando a red local: %s...\n", DEFAULT_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

    // Servidor MQTT configurado para Ubidots
    mqttClient.setServer(UBIDOTS_BROKER, UBIDOTS_PORT);
    mqttClient.setBufferSize(512);

    // Servidor Web Local
    servidorWeb.on("/", HTTP_GET, []() {
        servidorWeb.send(200, "text/html", generarDashboardHTML());
    });
    servidorWeb.on("/api/telemetria", HTTP_GET, []() {
        StaticJsonDocument<384> doc;
        doc["colmena"] = ultimaMedicionCache.colmena;
        doc["tempI"] = ultimaMedicionCache.tempInterna;
        doc["humI"] = ultimaMedicionCache.humInterna;
        doc["tempE"] = ultimaMedicionCache.tempExterna;
        doc["humE"] = ultimaMedicionCache.humExterna;
        doc["vpd"] = ultimaMedicionCache.vpdInterno;
        doc["gas"] = ultimaMedicionCache.cAire;
        doc["pir"] = ultimaMedicionCache.eventosPIRTotal;
        doc["lluvia"] = ultimaMedicionCache.llovio;
        String json;
        serializeJson(doc, json);
        servidorWeb.send(200, "application/json", json);
    });
    servidorWeb.begin();
    Serial.println("[OK] Servidor Web Local iniciado en puerto 80.");
}

void procesarComunicaciones() {
    // 1. Manejo del Servidor Web Local
    servidorWeb.handleClient();

    // 2. Reconexión persistente a WiFi
    if (WiFi.status() != WL_CONNECTED) {
        unsigned long ahora = millis();
        if (ahora - ultimoIntentoWiFi >= REINTENTO_WIFI_MS) {
            ultimoIntentoWiFi = ahora;
            Serial.println("[WiFi] Reintentando conexión...");
            WiFi.disconnect();
            WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
        }
        return;
    }

    // 3. Loop MQTT
    if (!mqttClient.connected()) {
        unsigned long ahora = millis();
        if (ahora - ultimoIntentoMQTT >= REINTENTO_MQTT_MS) {
            ultimoIntentoMQTT = ahora;
            Serial.print("[MQTT] Conectando a Ubidots Cloud...");
            // En Ubidots el usuario es el Token y la clave va vacía
            if (mqttClient.connect("InBeersaESP32", UBIDOTS_TOKEN, "")) {
                Serial.println(" ¡Conectado!");
            } else {
                Serial.printf(" Falló (rc=%d)\n", mqttClient.state());
            }
        }
    } else {
        mqttClient.loop();
    }
}

void atenderServidorWebLocal(const TelemetriaInBeersa &datos) {
    ultimaMedicionCache = datos;
}

String obtenerIPLocal() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0 (Desconectado)";
}

bool publicarUbidotsMQTT(const TelemetriaInBeersa &datos) {
    if (!mqttClient.connected()) {
        return false;
    }

    // Tópico Ubidots: /v1.6/devices/<device_label>
    char topic[64];
    snprintf(topic, sizeof(topic), "/v1.6/devices/%s", UBIDOTS_DEVICE_LABEL);

    // Payload optimizado Ubidots JSON
    StaticJsonDocument<384> doc;
    doc["temperatura_interna"] = round(datos.tempInterna * 10.0f) / 10.0f;
    doc["humedad_interna"]     = round(datos.humInterna * 10.0f) / 10.0f;
    doc["temperatura_externa"] = round(datos.tempExterna * 10.0f) / 10.0f;
    doc["humedad_externa"]     = round(datos.humExterna * 10.0f) / 10.0f;
    doc["vpd_nido"]            = round(datos.vpdInterno * 100.0f) / 100.0f;
    doc["delta_temperatura"]   = round(datos.deltaTemp * 10.0f) / 10.0f;
    doc["calidad_aire_gas"]    = round(datos.cAire * 10.0f) / 10.0f;
    doc["actividad_pir"]       = datos.pirDetectado;
    doc["lluvia_evento"]       = datos.llovio;

    char buffer[384];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqttClient.publish(topic, buffer);
    if (ok) {
        Serial.printf("[UBIDOTS] Datos telemetrizados con éxito: %s\n", buffer);
    } else {
        Serial.println("[ERROR] Falló publicación a Ubidots.");
    }
    return ok;
}

bool publicarHiveMQFallback(const TelemetriaInBeersa &datos, char *jsonBufferOut, size_t bufferSize) {
    StaticJsonDocument<384> doc;
    doc["colmena"]  = datos.colmena;
    doc["Fecha"]    = datos.fecha;
    doc["Hora"]     = datos.hora;
    doc["TempI"]    = round(datos.tempInterna * 10.0f) / 10.0f;
    doc["HumedadI"] = round(datos.humInterna * 10.0f) / 10.0f;
    doc["TempE"]    = round(datos.tempExterna * 10.0f) / 10.0f;
    doc["HumedadE"] = round(datos.humExterna * 10.0f) / 10.0f;
    doc["CAire"]    = round(datos.cAire * 10.0f) / 10.0f;
    doc["VPD"]      = round(datos.vpdInterno * 100.0f) / 100.0f;
    doc["PIR"]      = datos.pirDetectado;
    doc["Lluvia"]   = datos.llovio;

    serializeJson(doc, jsonBufferOut, bufferSize);
    return true;
}
