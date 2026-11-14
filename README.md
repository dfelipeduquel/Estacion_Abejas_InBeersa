# Estación InBeersa (*Melipona eburnea*)

Firmware modular en **C++17 / PlatformIO** sobre ESP32 con FreeRTOS para el monitoreo biometeorológico y bioacústico de colmenas de abejas angelitas (*Melipona eburnea*).

Desarrollado en el **Semillero Kilby** de la **Universidad Tecnológica de Pereira (UTP)** en colaboración con **TerraBit**.

---

## Arquitectura del Sistema

- **Núcleo y Concurrencia:** ESP32 dual-core sobre Arduino Framework (`-std=gnu++17`). Bucle principal no bloqueante (`millis()` / `vTaskDelay`) en Core 0 y tarea dedicada de adquisición bioacústica DMA I2S anclada en Core 1.
- **Psicrometría en Nido:** Adquisición termohigrométrica (DHT22) con filtro de retención de último valor válido ante lecturas atípicas y cálculo del Déficit de Presión de Vapor (VPD) mediante ecuación de Tetens.
- **Bioacústica I2S:** Captura PCM de 32 bits @ 16 kHz (INMP441) mediante buffers DMA en ráfagas de 5 segundos almacenadas en WAV sobre MicroSD.
- **Detección de Actividad:** Sensor PIR por interrupción de hardware (`IRAM_ATTR`) con debounce por software para conteo de tráfico en piquera.
- **Sincronización Temporal:** Reloj de tiempo real DS1307 (I2C) con actualización automática por NTP al enlazar a red Wi-Fi.
- **Almacenamiento Resiliente:** Registro JSON en tarjeta MicroSD (bus HSPI con auto-detección de mapeo de pines) y respaldo automático en memoria Flash interna (**LittleFS**).
- **Conectividad y Diagnóstico:** Modo concurrente Wi-Fi STA + SoftAP de campo, respondedor mDNS (`http://inbeersa.local`), servidor web local embebido en Flash y publicación MQTT hacia Ubidots Industrial.

---

## Mapa de Hardware (GPIO)

| Subsistema / Sensor | Señal | GPIO | Notas |
|---|---|---|---|
| **DHT22 Interno** | DATA | `2` | Sensor principal en cámara de cría |
| **DHT22 Externo** *(opcional)* | DATA | `23` | Habilitable vía `HABILITAR_DHT_EXTERNO` |
| **PIR Piquera** | OUT | `27` | Interrupción en flanco de subida (`INPUT_PULLDOWN`) |
| **Sensor Lluvia** *(opcional)* | D0 | `35` | Requiere pull-up externo `10 kΩ` a `3.3V` |
| **Micrófono INMP441 (I2S)** | SCK / WS / SD | `18` / `32` / `26` | Captura DMA en Core 1 |
| **RTC DS1307 (I2C)** | SDA / SCL | `5` / `4` | Dirección `0x68` |
| **MicroSD (HSPI Primario)** | SCK / MOSI / MISO / CS | `14` / `13` / `16` / `17` | Sondeo automático con fallback a `12 / 17 / 16 / 13` |
| **LEDs HMI** | KeepAlive / RTC / DHTI / DHTE / SD | `21` / `19` / `22` / `25` / `33` | LEDs de falla con lógica inversa (`LOW = ON`) |

---

## Estructura del Repositorio

```text
Estacion_InBeersa/
├── include/
│   ├── config_pins.h         # Mapeo de pines, flags de sensores y parámetros de red
│   ├── secrets.h.example     # Plantilla de credenciales (copiar a secrets.h)
│   ├── globals.h             # Estructuras de telemetría y primitivas FreeRTOS
│   └── ...                   # Cabeceras modulares (audio_i2s, comms, hmi, sensors, storage)
├── src/
│   ├── main.cpp              # Orquestación principal y temporizadores no bloqueantes
│   ├── audio_i2s.cpp         # Driver I2S DMA y escritura de archivos WAV
│   ├── comms.cpp             # Wi-Fi STA/AP, mDNS, Servidor Web HTTP y cliente MQTT
│   ├── hmi.cpp               # Indicadores LED de estado y diagnóstico
│   ├── sensors.cpp           # Drivers DHT22, PIR, RTC DS1307, NTP y cálculo VPD
│   └── storage.cpp           # Persistencia MicroSD FAT32 y respaldo LittleFS
├── tools/
│   └── calibracion/          # Utilidad standalone para calibración de PIR y sensor de lluvia
├── docs/
│   ├── CHANGELOG.md
│   ├── DOSSIER_MIGRACION.md
│   └── INFORME_CERTIFICACION_NOCTURNA.md
├── platformio.ini
└── LICENSE
```

---

## Configuración y Despliegue

### 1. Configurar credenciales privadas

Copie la plantilla de secretos antes de compilar:

```bash
cp include/secrets.h.example include/secrets.h
```

Edite `include/secrets.h` con el SSID y contraseña de su red Wi-Fi, la clave del Punto de Acceso local y su token de Ubidots. Este archivo está excluido de control de versiones mediante `.gitignore`.

### 2. Compilar y cargar firmware principal

Requiere **PlatformIO CLI** o la extensión de PlatformIO en VS Code:

```bash
# Compilar firmware principal (entorno por defecto: esp32dev)
pio run

# Cargar a la placa ESP32
pio run --target upload

# Abrir monitor serial (115200 baud)
pio device monitor
```

### 3. Utilidad de calibración de sensores (opcional)

```bash
pio run -e calibracion --target upload
pio device monitor -e calibracion
```

Consulte [tools/calibracion/README.md](tools/calibracion/README.md) para los modos de diagnóstico disponibles.

---

## Documentación Técnica

- [Bitácora de Ingeniería (Changelog)](docs/CHANGELOG.md)
- [Dossier de Migración MicroPython → C++17](docs/DOSSIER_MIGRACION.md)
- [Informe de Certificación en Campo](docs/INFORME_CERTIFICACION_NOCTURNA.md)

---

## Licencia y Autoría

Distribuido bajo licencia **MIT** — ver [LICENSE](LICENSE).

- **Autor y Líder Técnico:** Daniel Felipe Duque Lizarralde
- **Grupo de Investigación:** Semillero Kilby — Universidad Tecnológica de Pereira (UTP)
