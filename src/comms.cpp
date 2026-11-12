/**
 * @file comms.cpp
 * @brief Implementación de WiFi con diagnóstico de escaneo, Fallback AP automático y Ubidots.
 */

#include "comms.h"
#include "sensors.h"
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);
static WebServer servidorWeb(80);

static unsigned long ultimoIntentoWiFi = 0;
static const unsigned long REINTENTO_WIFI_MS = 12000;

static unsigned long ultimoIntentoMQTT = 0;
static const unsigned long REINTENTO_MQTT_MS = 8000;

static uint8_t intentosFallidosConexion = 0;
static const uint8_t MAX_INTENTOS_ANTES_DE_AP = 2; // Tras ~25s sin conectar, levanta AP de emergencia

static bool modoFallbackAPActivo = false;
static TelemetriaInBeersa ultimaMedicionCache = {};

// =============================================================================
// ESCANEO DIAGNÓSTICO DE REDES EN EL ARRANQUE
// =============================================================================
void escanearRedesWiFiDiagnostico() {
    Serial.println("\n[WiFi-SCAN] Iniciando escaneo de radiofrecuencia (2.4 GHz)...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();
    Serial.printf("[WiFi-SCAN] Escaneo completado. Redes detectadas: %d\n", n);
    if (n == 0) {
        Serial.println("[WiFi-SCAN] ¡ADVERTENCIA! No se detectaron redes en 2.4 GHz.");
    } else {
        bool encontradaTerraBit = false;
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            int32_t rssi = WiFi.RSSI(i);
            uint8_t canal = WiFi.channel(i);
            wifi_auth_mode_t auth = WiFi.encryptionType(i);

            const char *authStr = "Abierta";
            if (auth == WIFI_AUTH_WEP) authStr = "WEP";
            else if (auth == WIFI_AUTH_WPA_PSK) authStr = "WPA";
            else if (auth == WIFI_AUTH_WPA2_PSK) authStr = "WPA2";
            else if (auth == WIFI_AUTH_WPA_WPA2_PSK) authStr = "WPA/WPA2";
            else if (auth == WIFI_AUTH_WPA2_ENTERPRISE) authStr = "Enterprise";
            else if (auth == WIFI_AUTH_WPA3_PSK) authStr = "WPA3";

            Serial.printf("  [%d] SSID: %-20s | RSSI: %4d dBm | Canal: %2d | Cifrado: %s\n",
                          i + 1, ssid.c_str(), rssi, canal, authStr);

            if (ssid == DEFAULT_WIFI_SSID) {
                encontradaTerraBit = true;
                Serial.printf("  >>> RED OBJETIVO '%s' DETECTADA con senal: %d dBm en canal %d <<<\n",
                              DEFAULT_WIFI_SSID, rssi, canal);
            }
        }
        if (!encontradaTerraBit) {
            Serial.printf("[WiFi-SCAN] ALERTA: La red '%s' NO fue vista en el escaneo 2.4 GHz.\n", DEFAULT_WIFI_SSID);
        }
    }
    Serial.println("[WiFi-SCAN] Fin del reporte de diagnóstico.\n");
}

// =============================================================================
// DASHBOARD WEB LOCAL (LAN O AP DE EMERGENCIA)
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
        .badge { display: inline-block; padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: bold; }
        .badge-online { background: #238636; color: #fff; }
        .badge-ap { background: #d29922; color: #000; }
        .footer { text-align: center; font-size: 11px; color: #8b949e; margin-top: 24px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>🐝 InBeersa - Nodo Melipona</h1>
            <div class="subtitle">Semillero Kilby UTP | Monitoreo Bio-IoT 24/7</div>
            <div>Modo Red: %MODORED%</div>
            <p style="font-size:12px; color:#8b949e;">Fecha / Hora RTC: <strong>%FECHA% %HORA%</strong></p>
            
            <div class="grid">
                <div class="metric" style="border-color:#58a6ff;">
                    <div class="metric-title">Nido Interno</div>
                    <div class="metric-value">%TEMPI% <span class="metric-unit">°C</span></div>
                    <div style="font-size:13px; color:#8b949e;">HR: %HUMI% %%</div>
                </div>
                <div class="metric" style="border-color:#f0883e;">
                    <div class="metric-title">VPD en Nido</div>
                    <div class="metric-value">%VPD% <span class="metric-unit">kPa</span></div>
                    <div style="font-size:13px; color:#8b949e;">Confort Larval</div>
                </div>
                <div class="metric" style="border-color:#bc8cff;">
                    <div class="metric-title">Movimiento Piquera</div>
                    <div class="metric-value">%PIR% <span class="metric-unit">eventos</span></div>
                    <div style="font-size:13px; color:#8b949e;">Estado: %ESTADOPIR%</div>
                </div>
                <div class="metric" style="border-color:#ff7b72;">
                    <div class="metric-title">Sensor Lluvia</div>
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
    html.replace("%VPD%", String(ultimaMedicionCache.vpdInterno, 2));
    html.replace("%PIR%", String(ultimaMedicionCache.eventosPIRTotal));
    html.replace("%ESTADOPIR%", ultimaMedicionCache.pirDetectado ? "ACTIVO" : "REPOSO");
    html.replace("%LLUVIA%", ultimaMedicionCache.llovio ? "Lloviendo 🌧️" : "Seco ☀️");
    html.replace("%UPTIME%", String(ultimaMedicionCache.uptimeSeg));

    if (modoFallbackAPActivo) {
        html.replace("%MODORED%", "<span class='badge badge-ap'>AP AUXILIAR (192.168.4.1)</span>");
    } else {
        html.replace("%MODORED%", "<span class='badge badge-online'>EN LÍNEA (TerraBit LAN)</span>");
    }
    return html;
}

// =============================================================================
// INICIALIZACIÓN Y GESTIÓN DE CONECTIVIDAD
// =============================================================================
static void activarModoFallbackAP() {
    if (modoFallbackAPActivo) return;

    modoFallbackAPActivo = true;
    Serial.println("\n[AP-FALLBACK] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("[AP-FALLBACK] No fue posible autenticar con la red 'TerraBit'.");
    Serial.println("[AP-FALLBACK] Levantando Punto de Acceso (AP) de Emergencia...");
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
    delay(200);

    Serial.printf("[AP-FALLBACK] SSID Local  : %s\n", FALLBACK_AP_SSID);
    Serial.printf("[AP-FALLBACK] Contraseña  : %s\n", FALLBACK_AP_PASS);
    Serial.printf("[AP-FALLBACK] IP de Acceso: http://%s/\n", WiFi.softAPIP().toString().c_str());
    Serial.println("[AP-FALLBACK] Puedes conectar tu celular a esta red para ver el Dashboard.");
    Serial.println("[AP-FALLBACK] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
}

void inicializarComunicaciones() {
    // 1. Diagnóstico de espectro electromagnético
    escanearRedesWiFiDiagnostico();

    // 2. Secuencia estricta de arranque Wi-Fi
    Serial.printf("[WiFi] Conectando limpiamente a: '%s'...\n", DEFAULT_WIFI_SSID);
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.setTxPower(DEFAULT_WIFI_POWER);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

    // 3. Servidor MQTT Ubidots
    mqttClient.setServer(UBIDOTS_BROKER, UBIDOTS_PORT);
    mqttClient.setBufferSize(512);

    // 4. Servidor Web Local
    servidorWeb.on("/", HTTP_GET, []() {
        servidorWeb.send(200, "text/html", generarDashboardHTML());
    });
    servidorWeb.on("/api/telemetria", HTTP_GET, []() {
        StaticJsonDocument<384> doc;
        doc["colmena"] = ultimaMedicionCache.colmena;
        doc["tempI"] = ultimaMedicionCache.tempInterna;
        doc["humI"] = ultimaMedicionCache.humInterna;
        doc["vpd"] = ultimaMedicionCache.vpdInterno;
        doc["pir"] = ultimaMedicionCache.eventosPIRTotal;
        doc["lluvia"] = ultimaMedicionCache.llovio;
        String json;
        serializeJson(doc, json);
        servidorWeb.send(200, "application/json", json);
    });
    servidorWeb.begin();
    Serial.println("[OK] Servidor Web Local listo en puerto 80.");
}

void procesarComunicaciones() {
    // 1. Atender peticiones del servidor web (tanto en LAN como en AP)
    servidorWeb.handleClient();

    static bool wifiConectadoPrevio = false;

    // 2. Gestión de reconexión WiFi en modo STA
    if (WiFi.status() != WL_CONNECTED) {
        wifiConectadoPrevio = false;
        unsigned long ahora = millis();
        if (ahora - ultimoIntentoWiFi >= REINTENTO_WIFI_MS) {
            ultimoIntentoWiFi = ahora;
            intentosFallidosConexion++;
            Serial.printf("[WiFi] Reintentando asociacion a '%s' (Intento #%d)...\n",
                          DEFAULT_WIFI_SSID, intentosFallidosConexion);

            if (intentosFallidosConexion >= MAX_INTENTOS_ANTES_DE_AP && !modoFallbackAPActivo) {
                activarModoFallbackAP();
            }

            WiFi.disconnect();
            WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
        }
        return;
    }

    // Flanco de conexión: disparar acciones una sola vez al enlazar a la red WiFi
    if (!wifiConectadoPrevio) {
        wifiConectadoPrevio = true;
        intentosFallidosConexion = 0;
        Serial.printf("[WiFi] Conectado exitosamente a '%s'! IP asignada: %s\n",
                      DEFAULT_WIFI_SSID, WiFi.localIP().toString().c_str());

        // Automatización NTP: Sincronizar RTC DS1307 al enlazar a WiFi
        Serial.println("[NTP] Enlace WiFi detectado. Sincronizando RTC DS1307 con servidores de tiempo...");
        sincronizarRTCPorNTP();
    }

    // 3. Loop MQTT Ubidots
    if (!mqttClient.connected()) {
        unsigned long ahora = millis();
        if (ahora - ultimoIntentoMQTT >= REINTENTO_MQTT_MS) {
            ultimoIntentoMQTT = ahora;
            Serial.print("[MQTT] Conectando a Ubidots Cloud...");
            if (mqttClient.connect("InBeersaESP32", UBIDOTS_TOKEN, "")) {
                Serial.println(" Conectado!");
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
    } else if (modoFallbackAPActivo) {
        return WiFi.softAPIP().toString() + " (AP Auxiliar)";
    }
    return "0.0.0.0 (Desconectado)";
}

bool estaEnModoFallbackAP() {
    return modoFallbackAPActivo;
}

bool publicarUbidotsMQTT(const TelemetriaInBeersa &datos) {
    if (!mqttClient.connected()) {
        return false;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "/v1.6/devices/%s", UBIDOTS_DEVICE_LABEL);

    StaticJsonDocument<384> doc;
    doc["temperatura_interna"] = round(datos.tempInterna * 10.0f) / 10.0f;
    doc["humedad_interna"]     = round(datos.humInterna * 10.0f) / 10.0f;
    doc["vpd_nido"]            = round(datos.vpdInterno * 100.0f) / 100.0f;
    doc["actividad_pir"]       = datos.pirDetectado;
    doc["lluvia_evento"]       = datos.llovio;

    char buffer[384];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqttClient.publish(topic, buffer);
    if (ok) {
        Serial.printf("[UBIDOTS] Datos telemetrizados con éxito: %s\n", buffer);
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
    doc["VPD"]      = round(datos.vpdInterno * 100.0f) / 100.0f;
    doc["PIR"]      = datos.pirDetectado;
    doc["Lluvia"]   = datos.llovio;

    serializeJson(doc, jsonBufferOut, bufferSize);
    return true;
}
