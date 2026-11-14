/**
 * @file comms.cpp
 * @brief Implementación de WiFi con diagnóstico de escaneo, Fallback AP automático y Ubidots.
 */

#include "comms.h"
#include "sensors.h"
#include "storage.h"
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <LittleFS.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);
static WebServer servidorWeb(80);

static unsigned long ultimoIntentoWiFi = 0;
static const unsigned long REINTENTO_WIFI_MS = 12000;

static unsigned long ultimoIntentoMQTT = 0;
static const unsigned long REINTENTO_MQTT_MS = 8000;

static uint8_t intentosFallidosConexion = 0;
static const uint8_t MAX_INTENTOS_ANTES_DE_AP = 2;

static bool modoFallbackAPActivo = true; // El AP está activo por defecto en modo dual AP+STA
static TelemetriaInBeersa ultimaMedicionCache = {};

// Declaración forward
static void configurarRutasServidorWeb();

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
        bool redObjetivoEncontrada = false;
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
                redObjetivoEncontrada = true;
                Serial.printf("  >>> RED OBJETIVO '%s' DETECTADA con senal: %d dBm en canal %d <<<\n",
                              DEFAULT_WIFI_SSID, rssi, canal);
            }
        }
        if (!redObjetivoEncontrada) {
            Serial.printf("[WiFi-SCAN] ALERTA: La red '%s' NO fue vista en el escaneo 2.4 GHz.\n", DEFAULT_WIFI_SSID);
        }
    }
    Serial.println("[WiFi-SCAN] Fin del reporte de diagnóstico.\n");
}

// =============================================================================
// DASHBOARD WEB LOCAL DE ÉLITE (TEMÁTICA MIEL & ABEJAS ANGELITAS)
// =============================================================================
static const char HTML_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>InBeersa • Monitor Meliponas</title>
    <style>
        :root {
            --bg: #0b0f17;
            --surface: #121824;
            --surface-hover: #182234;
            --border: #1f2a3e;
            --honey: #f59e0b;
            --honey-light: #fbbf24;
            --honey-dark: #d97706;
            --honey-glow: rgba(245, 158, 11, 0.2);
            --text-main: #f9fafb;
            --text-muted: #9ca3af;
            --success: #10b981;
            --danger: #ef4444;
            --card-border: rgba(245, 158, 11, 0.22);
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg);
            color: var(--text-main);
            padding: 16px;
            line-height: 1.5;
        }
        .container { max-width: 860px; margin: 0 auto; }
        
        /* Encabezado */
        .header {
            background: linear-gradient(135deg, #172133 0%, #111723 100%);
            border: 1px solid var(--border);
            border-top: 4px solid var(--honey);
            border-radius: 16px;
            padding: 22px;
            margin-bottom: 18px;
            box-shadow: 0 10px 25px rgba(0,0,0,0.4);
            position: relative;
            overflow: hidden;
        }
        .header::after {
            content: '⬡';
            position: absolute;
            right: 15px;
            top: -15px;
            font-size: 130px;
            color: rgba(245, 158, 11, 0.04);
            pointer-events: none;
        }
        .title-row { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; }
        .logo-wrap { display: flex; align-items: center; gap: 12px; }
        .logo-icon { font-size: 34px; line-height: 1; filter: drop-shadow(0 0 10px rgba(245,158,11,0.5)); }
        h1 { font-size: 22px; font-weight: 700; color: var(--honey-light); letter-spacing: 0.3px; }
        .subtitle { font-size: 13px; color: var(--text-muted); margin-top: 2px; }
        
        /* Badges de Conectividad */
        .status-pills { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 14px; }
        .pill {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            padding: 5px 12px;
            border-radius: 20px;
            font-size: 11px;
            font-weight: 600;
            background: #0f141f;
            border: 1px solid var(--border);
            color: var(--text-muted);
        }
        .pill-gold { border-color: rgba(245,158,11,0.4); color: var(--honey-light); }
        .dot { width: 7px; height: 7px; border-radius: 50%; background: var(--success); }
        .dot-pulse { animation: pulse 2s infinite; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
        
        /* Grid de Métricas */
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 14px; margin-bottom: 20px; }
        .card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 14px;
            padding: 18px;
            transition: transform 0.15s, border-color 0.15s, box-shadow 0.15s;
            position: relative;
            overflow: hidden;
        }
        .card:hover {
            border-color: rgba(245, 158, 11, 0.45);
            box-shadow: 0 4px 18px var(--honey-glow);
        }
        .card-top { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
        .card-title { font-size: 12px; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-muted); font-weight: 600; }
        .card-icon { font-size: 22px; }
        .card-val { font-size: 32px; font-weight: 700; color: #fff; display: flex; align-items: baseline; gap: 4px; }
        .card-unit { font-size: 14px; color: var(--text-muted); font-weight: 500; }
        .card-footer { font-size: 12px; color: var(--honey); margin-top: 6px; display: flex; align-items: center; gap: 4px; }
        .badge-sub { display: inline-block; padding: 2px 8px; border-radius: 8px; font-size: 10px; font-weight: 700; background: rgba(245, 158, 11, 0.15); color: var(--honey-light); }
        
        /* Barra de Acciones */
        .actions-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 14px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .actions-title { font-size: 14px; font-weight: 700; color: var(--honey-light); margin-bottom: 14px; display: flex; align-items: center; gap: 8px; }
        .btn-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; }
        .btn {
            background: #182030;
            border: 1px solid var(--border);
            color: var(--text-main);
            padding: 10px 14px;
            border-radius: 10px;
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
            text-decoration: none;
            transition: all 0.2s;
        }
        .btn:hover { background: #222d42; border-color: var(--honey); color: var(--honey-light); }
        .btn-primary { background: var(--honey-dark); border-color: var(--honey); color: #000; font-weight: 700; }
        .btn-primary:hover { background: var(--honey-light); color: #000; }
        .btn-danger:hover { border-color: var(--danger); color: var(--danger); }
        
        /* Tabla de Historial */
        .history-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 14px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .table-wrap { overflow-x: auto; margin-top: 12px; }
        table { width: 100%; border-collapse: collapse; font-size: 12px; text-align: left; }
        th { background: #0f141f; color: var(--honey-light); padding: 10px; font-weight: 600; border-bottom: 2px solid var(--border); }
        td { padding: 9px 10px; border-bottom: 1px solid rgba(255,255,255,0.04); color: var(--text-main); }
        tr:hover td { background: rgba(245, 158, 11, 0.05); }
        
        /* Toast Notification */
        .toast {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background: #172133;
            border: 1px solid var(--honey);
            color: var(--honey-light);
            padding: 12px 20px;
            border-radius: 10px;
            font-size: 13px;
            font-weight: 600;
            box-shadow: 0 8px 24px rgba(0,0,0,0.6);
            display: none;
            z-index: 1000;
        }
        
        .footer { text-align: center; font-size: 11px; color: var(--text-muted); margin-top: 10px; padding-bottom: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <!-- Encabezado con Identidad InBeersa -->
        <div class="header">
            <div class="title-row">
                <div class="logo-wrap">
                    <div class="logo-icon">🐝</div>
                    <div>
                        <h1>INBEERSA • BIOMONITOR</h1>
                        <div class="subtitle">Estación Bioacústica y Bioclimática • <i>Melipona eburnea</i> (Abejas Angelitas)</div>
                    </div>
                </div>
            </div>
            
            <div class="status-pills">
                <div class="pill pill-gold">
                    <span class="dot dot-pulse"></span>
                    <span>AP: %SSID_AP%</span>
                </div>
                <div class="pill">
                    <span>🔑 Clave: %PASS_AP%</span>
                </div>
                <div class="pill">
                    <span>🌐 IP Local: %IP_LAN%</span>
                </div>
                <div class="pill">
                    <span>🕒 RTC DS1307: <strong id="val-reloj">%FECHA% %HORA%</strong></span>
                </div>
            </div>
        </div>

        <!-- Grid de Métricas Principales -->
        <div class="grid">
            <!-- Nido Interno -->
            <div class="card" style="border-top: 3px solid #f59e0b;">
                <div class="card-top">
                    <span class="card-title">Nido Interno</span>
                    <span class="card-icon">🌡️</span>
                </div>
                <div class="card-val"><span id="val-tempi">%TEMPI%</span> <span class="card-unit">°C</span></div>
                <div class="card-footer">
                    <span class="badge-sub">HR: <span id="val-humi">%HUMI%</span> %</span>
                    <span>Confort Nido</span>
                </div>
            </div>

            <!-- VPD Tetens -->
            <div class="card" style="border-top: 3px solid #fbbf24;">
                <div class="card-top">
                    <span class="card-title">VPD en Nido (Tetens)</span>
                    <span class="card-icon">🍯</span>
                </div>
                <div class="card-val"><span id="val-vpd">%VPD%</span> <span class="card-unit">kPa</span></div>
                <div class="card-footer">
                    <span class="badge-sub">Óptimo: 0.6 - 1.1 kPa</span>
                </div>
            </div>

            <!-- Actividad en Piquera -->
            <div class="card" style="border-top: 3px solid #10b981;">
                <div class="card-top">
                    <span class="card-title">Actividad Piquera (PIR)</span>
                    <span class="card-icon">🐝</span>
                </div>
                <div class="card-val"><span id="val-pir">%PIR%</span> <span class="card-unit">eventos</span></div>
                <div class="card-footer">
                    <span class="badge-sub" id="val-estadopir">%ESTADOPIR%</span>
                    <span>GPIO 27 (Filtrado)</span>
                </div>
            </div>

            <!-- Clima y Lluvia -->
            <div class="card" style="border-top: 3px solid #3b82f6;">
                <div class="card-top">
                    <span class="card-title">Precipitación</span>
                    <span class="card-icon">🌧️</span>
                </div>
                <div class="card-val" style="font-size:24px;" id="val-lluvia">%LLUVIA%</div>
                <div class="card-footer">
                    <span class="badge-sub">Sensor J8</span>
                    <span>Uptime: <span id="val-uptime">%UPTIME%</span>s</span>
                </div>
            </div>

            <!-- Calidad del Aire (MQ-135) -->
            <div class="card" style="border-top: 3px solid #8b5cf6;">
                <div class="card-top">
                    <span class="card-title">Calidad Aire (MQ-135)</span>
                    <span class="card-icon">💨</span>
                </div>
                <div class="card-val"><span id="val-caire">%AIRE%</span> <span class="card-unit">raw</span></div>
                <div class="card-footer">
                    <span class="badge-sub">Filtro EMA α=0.2</span>
                </div>
            </div>

            <!-- Enlace IoT Ubidots -->
            <div class="card" style="border-top: 3px solid #ec4899;">
                <div class="card-top">
                    <span class="card-title">Enlace Telemetría</span>
                    <span class="card-icon">📡</span>
                </div>
                <div class="card-val" style="font-size:22px;" id="val-enlace">Activo</div>
                <div class="card-footer">
                    <span class="badge-sub">Ubidots Industrial</span>
                    <span>Cadencia: 5 min</span>
                </div>
            </div>
        </div>

        <!-- Centro de Acciones de Élite -->
        <div class="actions-card">
            <div class="actions-title">⚡ Herramientas y Acciones de Mantenimiento</div>
            <div class="btn-grid">
                <a href="/descargar-datos" download="inbeersa_data.json" class="btn btn-primary">
                    📥 Descargar JSON
                </a>
                <button onclick="ejecutarAccion('/api/test-audio', '¿Disparar ráfaga bioacústica de prueba (5s)?')" class="btn">
                    🎙️ Grabar Audio WAV
                </button>
                <button onclick="ejecutarAccion('/api/sync-ntp', '¿Sincronizar hora con servidor NTP atómico?')" class="btn">
                    ⏰ Sincronizar NTP
                </button>
                <button onclick="ejecutarAccion('/api/limpiar-sd', '¡ATENCIÓN! ¿Estás seguro de vaciar el archivo data.json en la MicroSD?')" class="btn btn-danger">
                    🧹 Purgar MicroSD
                </button>
                <button onclick="ejecutarAccion('/api/reiniciar', '¿Reiniciar la estación InBeersa ahora?')" class="btn btn-danger">
                    🔄 Reiniciar Nodo
                </button>
            </div>
        </div>

        <!-- Tabla de Historial en Vivo -->
        <div class="history-card">
            <div class="actions-title">📊 Últimos Registros Biometeorológicos (MicroSD)</div>
            <div class="table-wrap">
                <table>
                    <thead>
                        <tr>
                            <th>Hora RTC</th>
                            <th>Nido (°C)</th>
                            <th>Humedad (%)</th>
                            <th>VPD (kPa)</th>
                            <th>PIR (Actividad)</th>
                            <th>Lluvia</th>
                        </tr>
                    </thead>
                    <tbody id="historial-tbody">
                        <tr><td colspan="6" style="text-align:center; color:#8b949e;">Cargando registros recientes...</td></tr>
                    </tbody>
                </table>
            </div>
        </div>

        <!-- Toast Floating Notification -->
        <div id="toast" class="toast">Mensaje del sistema</div>

        <!-- Footer -->
        <div class="footer">
            InBeersa v%VERSION% • Ecosistema TerraBit & Semillero Kilby (UTP Pereira) • 2026
        </div>
    </div>

    <script>
        function showToast(msg) {
            const t = document.getElementById('toast');
            t.innerText = msg;
            t.style.display = 'block';
            setTimeout(() => { t.style.display = 'none'; }, 4000);
        }

        async function ejecutarAccion(url, confirmMsg) {
            if (confirmMsg && !confirm(confirmMsg)) return;
            showToast("Ejecutando acción...");
            try {
                const res = await fetch(url, { method: 'POST' });
                const data = await res.json();
                showToast(data.mensaje || "Acción completada con éxito");
                setTimeout(cargarHistorial, 1500);
            } catch (e) {
                showToast("Acción enviada al nodo.");
            }
        }

        async function actualizarMetricas() {
            try {
                const res = await fetch('/api/telemetria');
                if (!res.ok) return;
                const d = await res.json();
                document.getElementById('val-tempi').innerText = d.tempI.toFixed(1);
                document.getElementById('val-humi').innerText = d.humI.toFixed(1);
                document.getElementById('val-vpd').innerText = d.vpd.toFixed(2);
                document.getElementById('val-pir').innerText = d.pir;
                document.getElementById('val-estadopir').innerText = d.pirActivo ? "ACTIVO" : "REPOSO";
                document.getElementById('val-lluvia').innerText = d.lluvia ? "Lloviendo 🌧️" : "Seco ☀️";
                document.getElementById('val-caire').innerText = d.cAire.toFixed(1);
                document.getElementById('val-uptime').innerText = d.uptime;
                document.getElementById('val-reloj').innerText = d.fecha + " " + d.hora;
            } catch(e) {}
        }

        async function cargarHistorial() {
            try {
                const res = await fetch('/api/historial');
                if (!res.ok) return;
                const lista = await res.json();
                const tbody = document.getElementById('historial-tbody');
                if (!lista || lista.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; color:#8b949e;">No hay datos registrados aún.</td></tr>';
                    return;
                }
                let html = '';
                lista.slice().reverse().forEach(row => {
                    const temp = (row.TempI !== undefined) ? Number(row.TempI).toFixed(1) : '--';
                    const hum  = (row.HumedadI !== undefined) ? Number(row.HumedadI).toFixed(1) : '--';
                    const vpd  = (row.VPD !== undefined) ? Number(row.VPD).toFixed(2) : '--';
                    const pir  = (row.PIR !== undefined) ? row.PIR : '0';
                    const luv  = row.Lluvia ? '🌧️ Sí' : '☀️ No';
                    html += `<tr>
                        <td><strong>${row.Hora || '--'}</strong></td>
                        <td style="color:#fbbf24;">${temp} °C</td>
                        <td>${hum} %</td>
                        <td>${vpd} kPa</td>
                        <td>${pir}</td>
                        <td>${luv}</td>
                    </tr>`;
                });
                tbody.innerHTML = html;
            } catch(e) {}
        }

        // Auto-refresco
        setInterval(actualizarMetricas, 4000);
        setInterval(cargarHistorial, 10000);
        cargarHistorial();
    </script>
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
    html.replace("%AIRE%", String(ultimaMedicionCache.cAire, 1));
    html.replace("%UPTIME%", String(ultimaMedicionCache.uptimeSeg));

    html.replace("%SSID_AP%", FALLBACK_AP_SSID);
    html.replace("%PASS_AP%", FALLBACK_AP_PASS);
    html.replace("%VERSION%", FIRMWARE_VERSION);

    if (WiFi.status() == WL_CONNECTED) {
        html.replace("%IP_LAN%", WiFi.localIP().toString());
    } else {
        html.replace("%IP_LAN%", "192.168.4.1 (AP Activo)");
    }

    return html;
}

// =============================================================================
// RUTAS Y ENDPOINTS DEL SERVIDOR WEB LOCAL
// =============================================================================
static void configurarRutasServidorWeb() {
    // 1. Dashboard Principal
    servidorWeb.on("/", HTTP_GET, []() {
        servidorWeb.send(200, "text/html", generarDashboardHTML());
    });

    // 2. API Telemetría en Vivo (JSON)
    servidorWeb.on("/api/telemetria", HTTP_GET, []() {
        StaticJsonDocument<384> doc;
        doc["colmena"] = ultimaMedicionCache.colmena;
        doc["fecha"] = ultimaMedicionCache.fecha;
        doc["hora"] = ultimaMedicionCache.hora;
        doc["tempI"] = round(ultimaMedicionCache.tempInterna * 10.0f) / 10.0f;
        doc["humI"] = round(ultimaMedicionCache.humInterna * 10.0f) / 10.0f;
        doc["vpd"] = round(ultimaMedicionCache.vpdInterno * 100.0f) / 100.0f;
        doc["pir"] = ultimaMedicionCache.eventosPIRTotal;
        doc["pirActivo"] = ultimaMedicionCache.pirDetectado;
        doc["lluvia"] = ultimaMedicionCache.llovio;
        doc["cAire"] = round(ultimaMedicionCache.cAire * 10.0f) / 10.0f;
        doc["uptime"] = millis() / 1000;
        doc["ipLocal"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "192.168.4.1 (AP)";
        doc["wifiOk"] = (WiFi.status() == WL_CONNECTED);
        String json;
        serializeJson(doc, json);
        servidorWeb.send(200, "application/json", json);
    });

    // 3. API Historial de Mediciones (Últimos registros de la SD)
    servidorWeb.on("/api/historial", HTTP_GET, []() {
        String historialJson = obtenerUltimosRegistrosJSON(10);
        servidorWeb.send(200, "application/json", historialJson);
    });

    // 4. Descarga Directa del Dataset JSON
    servidorWeb.on("/descargar-datos", HTTP_GET, []() {
        if (esSDDisponible() && SD.exists("/data.json")) {
            File f = SD.open("/data.json", FILE_READ);
            servidorWeb.sendHeader("Content-Disposition", "attachment; filename=\"inbeersa_data.json\"");
            servidorWeb.streamFile(f, "application/json");
            f.close();
        } else if (LittleFS.exists("/telemetria_backup.json")) {
            File f = LittleFS.open("/telemetria_backup.json", FILE_READ);
            servidorWeb.sendHeader("Content-Disposition", "attachment; filename=\"inbeersa_backup.json\"");
            servidorWeb.streamFile(f, "application/json");
            f.close();
        } else {
            servidorWeb.send(404, "text/plain", "No hay datos de telemetria disponibles.");
        }
    });

    // 5. Favicon mudo para evitar errores 404 en consola
    servidorWeb.on("/favicon.ico", HTTP_GET, []() {
        servidorWeb.send(204); // No Content
    });

    // 6. Disparo de Prueba Bioacústica Manual (INMP441 - Bypass de compuerta)
    servidorWeb.on("/api/test-audio", HTTP_ANY, []() {
        if (semaforoGrabacionAudio != nullptr) {
            banderaAudioManualWeb = true; // Activar bypass de compuerta temporal
            xSemaphoreGive(semaforoGrabacionAudio);
            servidorWeb.send(200, "application/json", "{\"status\":\"ok\",\"mensaje\":\"Ráfaga bioacústica disparada en Core 1 (Bypass activo).\"}");
        } else {
            servidorWeb.send(500, "application/json", "{\"status\":\"error\",\"mensaje\":\"Semáforo no disponible.\"}");
        }
    });

    // 7. Forzar Sincronización NTP
    servidorWeb.on("/api/sync-ntp", HTTP_ANY, []() {
        bool ok = sincronizarRTCPorNTP();
        servidorWeb.send(200, "application/json", ok ? "{\"status\":\"ok\",\"mensaje\":\"RTC sincronizado con hora atómica.\"}" : "{\"status\":\"error\",\"mensaje\":\"Servidor NTP no respondió.\"}");
    });

    // 8. Purgar / Limpiar MicroSD
    servidorWeb.on("/api/limpiar-sd", HTTP_ANY, []() {
        bool ok = limpiarRegistrosSD();
        servidorWeb.send(200, "application/json", ok ? "{\"status\":\"ok\",\"mensaje\":\"MicroSD purgada. Archivo data.json reinicializado.\"}" : "{\"status\":\"error\",\"mensaje\":\"Error al purgar MicroSD.\"}");
    });

    // 9. Reinicio Remoto de la Estación
    servidorWeb.on("/api/reiniciar", HTTP_ANY, []() {
        servidorWeb.send(200, "application/json", "{\"status\":\"ok\",\"mensaje\":\"Reiniciando estación InBeersa...\"}");
        delay(600);
        ESP.restart();
    });
}

// =============================================================================
// INICIALIZACIÓN Y GESTIÓN DE CONECTIVIDAD
// =============================================================================
static void activarModoFallbackAP() {
    Serial.printf("\n[AP-FALLBACK] Red '%s' fuera de alcance. Hotspot local operativo.\n", DEFAULT_WIFI_SSID);
}

void inicializarComunicaciones() {
    // 1. Diagnóstico de espectro electromagnético
    escanearRedesWiFiDiagnostico();

    // 2. Levantar Modo AP + STA Concurrente (Acceso local garantizado + enlace router)
    Serial.printf("[WiFi] Configurando AP Local: '%s'...\n", FALLBACK_AP_SSID);
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    // Iniciar SoftAP de campo
    WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
    delay(100);
    Serial.println("\n[AP-LOCAL] ===================================================");
    Serial.printf("[AP-LOCAL] Red Wi-Fi Directa : %s\n", FALLBACK_AP_SSID);
    Serial.printf("[AP-LOCAL] Clave de Acceso   : %s\n", FALLBACK_AP_PASS);
    Serial.printf("[AP-LOCAL] IP Directa Móvil  : http://%s/\n", WiFi.softAPIP().toString().c_str());
    Serial.println("[AP-LOCAL] ===================================================\n");

    // Conectar a red WiFi local (STA)
    WiFi.setTxPower(DEFAULT_WIFI_POWER);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

    // 3. Respondedor mDNS
    if (MDNS.begin("inbeersa")) {
        Serial.println("[mDNS] Respondedor activo en http://inbeersa.local/");
    }

    // 4. Servidor MQTT Ubidots
    mqttClient.setServer(UBIDOTS_BROKER, UBIDOTS_PORT);
    mqttClient.setBufferSize(512);

    // 5. Configurar endpoints del Servidor Web
    configurarRutasServidorWeb();
    servidorWeb.begin();
    Serial.println("[OK] Servidor Web Local listo en puerto 80 (HTTP).");
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

    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "{\"temperatura_interna\":%.1f,\"humedad_interna\":%.1f,\"vpd_nido\":%.2f,\"actividad_pir\":%d,\"lluvia_evento\":%d}",
        datos.tempInterna, datos.humInterna, datos.vpdInterno, datos.pirDetectado, datos.llovio);

    bool ok = mqttClient.publish(topic, buffer);
    if (ok) {
        Serial.printf("[UBIDOTS] Datos telemetrizados con éxito: %s\n", buffer);
    }
    return ok;
}

bool publicarHiveMQFallback(const TelemetriaInBeersa &datos, char *jsonBufferOut, size_t bufferSize) {
    snprintf(jsonBufferOut, bufferSize,
        "{\"colmena\":\"%s\",\"Fecha\":\"%s\",\"Hora\":\"%s\",\"TempI\":%.1f,\"HumedadI\":%.1f,\"VPD\":%.2f,\"PIR\":%d,\"Lluvia\":%d}",
        datos.colmena, datos.fecha, datos.hora,
        datos.tempInterna, datos.humInterna, datos.vpdInterno,
        datos.pirDetectado, datos.llovio);
    return true;
}
