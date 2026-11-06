/**
 * @file storage.cpp
 * @brief Implementación de persistencia local en MicroSD FAT32 y LittleFS.
 */

#include "storage.h"
#include "hmi.h"
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>

static SPIClass spiSD(HSPI);
static bool sdActiva = false;
static bool littleFsActivo = false;

bool inicializarAlmacenamiento() {
    Serial.println("[STORAGE] Inicializando almacenamiento masivo...");

    // 1. Inicializar LittleFS interno como partición de respaldo
    if (LittleFS.begin(true)) {
        littleFsActivo = true;
        Serial.println("[OK] Memoria Flash LittleFS en línea (Backup).");
    } else {
        Serial.println("[ERROR] No se pudo montar LittleFS interno.");
    }

    // 2. Inicializar Bus SPI para MicroSD (HSPI: SCK=12, MISO=16, MOSI=17)
    spiSD.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    uint8_t intentos = 0;
    while (intentos < 3 && !sdActiva) {
        if (SD.begin(PIN_SD_CS, spiSD, 10000000)) {
            sdActiva = true;
            Serial.println("[OK] Tarjeta MicroSD FAT32 montada correctamente.");
            setLedErrorSD(false);
            asegurarEstructuraCarpetasSD();
            break;
        }
        intentos++;
        delay(100);
    }

    if (!sdActiva) {
        Serial.println("[ERROR] No se detectó tarjeta MicroSD. Operando con LittleFS.");
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

    // Intentar guardar en MicroSD
    if (sdActiva) {
        File f = SD.open("/data.json", FILE_APPEND);
        if (f) {
            f.println(jsonString);
            f.flush();
            f.close();
            setLedErrorSD(false);
            exito = true;
        } else {
            Serial.println("[ERROR] Fallo al escribir en /data.json en MicroSD.");
            setLedErrorSD(true);
        }
    }

    // Si falló la SD o no está disponible, guardar en LittleFS de contingencia
    if (!exito && littleFsActivo) {
        File fBackup = LittleFS.open("/telemetria_backup.json", FILE_APPEND);
        if (fBackup) {
            fBackup.println(jsonString);
            fBackup.flush();
            fBackup.close();
            Serial.println("[BACKUP] Telemetría salvaguardada en LittleFS.");
            exito = true;
        }
    }

    return exito;
}
