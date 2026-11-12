/**
 * @file storage.cpp
 * @brief Implementación de persistencia local en MicroSD FAT32 con autodetención de pines y LittleFS.
 */

#include "storage.h"
#include "hmi.h"
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <vector>

static SPIClass spiSD(HSPI);
static bool sdActiva = false;
static bool littleFsActivo = false;

static bool intentarMontarSD(uint8_t pinSck, uint8_t pinMiso, uint8_t pinMosi, uint8_t pinCs, const char *nombreConfig) {
    Serial.printf("[SD-PROBE] Probando configuración %s: SCK=%d, MISO=%d, MOSI=%d, CS=%d...\n",
                  nombreConfig, pinSck, pinMiso, pinMosi, pinCs);
    
    spiSD.end();
    delay(50);
    spiSD.begin(pinSck, pinMiso, pinMosi, pinCs);
    pinMode(pinCs, OUTPUT);
    digitalWrite(pinCs, HIGH);
    delay(50);

    // Intentar a 400 kHz primero (estándar SD init)
    if (SD.begin(pinCs, spiSD, 400000)) {
        Serial.printf("[OK] Tarjeta MicroSD detectada exitosamente en %s!\n", nombreConfig);
        // Si el init tuvo éxito, reiniciar a velocidad de transferencia rápida
        SD.end();
        delay(50);
        if (SD.begin(pinCs, spiSD, 4000000)) {
            Serial.println("[OK] Bus SPI escalado a 4 MHz para alta velocidad.");
            return true;
        }
        return SD.begin(pinCs, spiSD, 400000); // Respaldo a velocidad segura
    }
    return false;
}

bool inicializarAlmacenamiento() {
    Serial.println("[STORAGE] Inicializando subsistema de almacenamiento...");

    // 1. Inicializar LittleFS interno siempre como respaldo garantizado
    if (LittleFS.begin(true)) {
        littleFsActivo = true;
        Serial.println("[OK] Memoria Flash LittleFS en línea (Backup).");
    } else {
        Serial.println("[ERROR] No se pudo montar LittleFS interno.");
    }

    // 2. Probar Mapeo Primario (Código funcional de campo: SCK=14, MOSI=13, MISO=16, CS=17)
    sdActiva = intentarMontarSD(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, "Primaria (SCK=14,CS=17)");

    // 3. Si falló, probar Mapeo Secundario (SCK=12, MOSI=17, MISO=16, CS=13)
    if (!sdActiva) {
        Serial.println("[ALERTA] Falló mapeo primario. Probando mapeo alternativo...");
        sdActiva = intentarMontarSD(PIN_SD_SCK_ALT, PIN_SD_MISO, PIN_SD_MOSI_ALT, PIN_SD_CS_ALT, "Alternativa (SCK=12,CS=13)");
    }

    if (sdActiva) {
        setLedErrorSD(false);
        asegurarEstructuraCarpetasSD();
    } else {
        Serial.println("[ALERTA] Tarjeta MicroSD no respondió. Operando de forma transparente en LittleFS.");
        setLedErrorSD(true);
    }

    return (sdActiva || littleFsActivo);
}

bool esSDDisponible() {
    return sdActiva;
}

bool asegurarEstructuraCarpetasSD() {
    if (!sdActiva) return false;

    if (!SD.exists("/Audios_Normal")) {
        SD.mkdir("/Audios_Normal");
    }
    if (!SD.exists("/Audios_PIR")) {
        SD.mkdir("/Audios_PIR");
    }
    return true;
}

bool guardarTelemetriaJSON(const char *jsonString) {
    bool exito = false;

    // 1. Guardar en MicroSD si está disponible
    if (sdActiva) {
        File f = SD.open("/data.json", FILE_APPEND);
        if (f) {
            f.println(jsonString);
            f.flush();
            f.close();
            setLedErrorSD(false);
            exito = true;
        } else {
            Serial.println("[ERROR] Error de escritura en MicroSD /data.json.");
            setLedErrorSD(true);
        }
    }

    // 2. Guardar en LittleFS (Siempre como salvaguarda inmutable)
    if (littleFsActivo) {
        File fBackup = LittleFS.open("/telemetria_backup.json", FILE_APPEND);
        if (fBackup) {
            fBackup.println(jsonString);
            fBackup.flush();
            fBackup.close();
            if (!exito) {
                Serial.println("[BACKUP] Telemetría salvaguardada en LittleFS.");
            }
            exito = true;
        }
    }

    return exito;
}

bool limpiarRegistrosSD() {
    bool ok = false;
    if (sdActiva) {
        if (SD.exists("/data.json")) {
            SD.remove("/data.json");
        }
        File f = SD.open("/data.json", FILE_WRITE);
        if (f) {
            f.close();
            Serial.println("[STORAGE] Archivo /data.json reinicializado en MicroSD.");
            ok = true;
        }
        asegurarEstructuraCarpetasSD();
    }
    if (littleFsActivo) {
        if (LittleFS.exists("/telemetria_backup.json")) {
            LittleFS.remove("/telemetria_backup.json");
        }
    }
    return ok;
}

String obtenerUltimosRegistrosJSON(size_t maxLineas) {
    File f;
    if (sdActiva && SD.exists("/data.json")) {
        f = SD.open("/data.json", FILE_READ);
    } else if (littleFsActivo && LittleFS.exists("/telemetria_backup.json")) {
        f = LittleFS.open("/telemetria_backup.json", FILE_READ);
    }

    if (!f) {
        return "[]";
    }

    size_t tamano = f.size();
    if (tamano == 0) {
        f.close();
        return "[]";
    }

    // Si el archivo es grande, saltar a los últimos 4 KB para capturar las últimas líneas
    const size_t VENTANA_LECTURA = 4096;
    if (tamano > VENTANA_LECTURA) {
        f.seek(tamano - VENTANA_LECTURA);
        f.readStringUntil('\n'); // Descartar línea parcial
    }

    std::vector<String> lineas;
    while (f.available()) {
        String linea = f.readStringUntil('\n');
        linea.trim();
        if (linea.length() > 5 && linea.startsWith("{") && linea.endsWith("}")) {
            lineas.push_back(linea);
            if (lineas.size() > maxLineas) {
                lineas.erase(lineas.begin());
            }
        }
    }
    f.close();

    String resultado = "[";
    for (size_t i = 0; i < lineas.size(); i++) {
        if (i > 0) resultado += ",";
        resultado += lineas[i];
    }
    resultado += "]";
    return resultado;
}
