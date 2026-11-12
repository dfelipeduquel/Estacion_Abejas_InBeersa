# Dossier de Transferencia Técnica — InBeersa v2.2.1 -> v2.3

## 1. Identidad del Proyecto y Ubicación
- **Nombre:** Estación de Monitoreo Bioacústico y Bioclimático InBeersa (*Melipona eburnea*)
- **Semillero / Entidad:** Semillero Kilby — Universidad Tecnológica de Pereira (UTP)
- **Repositorio Local Oficial:** `<RUTA_LOCAL>`
- **Workspace VS Code:** `<RUTA_LOCAL>`
- **Remoto GitHub:** `https://github.com/dfelipeduquel/Estacion_Abejas_InBeersa.git` (Rama `main`, commit `6e5b634`)
- **Acceso Directo en Documentos:** `<RUTA_LOCAL> ESTACIONES & DESARROLLO\InBeersa - Abejas Angelitas (Kilby UTP)`

---

## 2. Diagnóstico de la Prueba Nocturna (22 Horas en Vivo)
- **Muestras registradas:** 3,211 registros en `/data.json` (MicroSD FAT32).
- **Confiabilidad del DHT22:** 99.84% (solo 5 lecturas de -200°C en 22 horas).
- **Microclima medido:**
  - Temperatura: 22.40 °C – 25.70 °C (Promedio 23.00 °C)
  - Humedad: 71.60 % – 92.50 % (Promedio 83.55 %)
  - VPD: 0.21 kPa – 0.94 kPa (Promedio 0.46 kPa)
- **Carga de estrés:** 2.18 GB escritos en MicroSD (6,809 archivos WAV + JSON) sin congelamientos, caídas de RTOS ni fallos de memoria.

---

## 3. Hallazgos Forenses y Errores Detectados (A Corregir en v2.3)
1. **Cruce de Pines MicroSD SPI vs PIR:**
   - En `include/config_pins.h`, `PIN_PIR_MOVIMIENTO` estaba en `GPIO 14` y `PIN_SD_SCK` en `GPIO 14`.
   - El reloj SPI a 4 MHz disparaba la interrupción `isrPIR` en bucle infinito, provocando 6,809 grabaciones back-to-back.
   - **Corrección verificada:** En la PCB real (MicroPython original), el PIR físico es **GPIO 27**.
2. **Cruce de Pines del Micrófono I2S (INMP441):**
   - En `include/config_pins.h`, `PIN_I2S_SD` estaba en 27 y `PIN_I2S_WS` en 33.
   - **Corrección verificada:** En la PCB real:
     - `PIN_I2S_SCK = 18` (BCLK)
     - `PIN_I2S_WS  = 32` (LRCLK)
     - `PIN_I2S_SD  = 26` (DOUT / Data Input)
3. **Mapeo de la Tarjeta MicroSD (Verificado y Operativo):**
   - `SCK = 14`, `MOSI = 13`, `MISO = 16`, `CS = 17` (Bus HSPI a 4 MHz).
4. **Reloj DS1307:**
   - Operativo en `SDA = 5`, `SCL = 4`. Requiere llamada a `sincronizarHoraConNTP()` una vez conecte a WiFi.

---

## 4. Tareas Inmediatas de Firmware (Sprint v2.3)
1. Aplicar la reasignación de pines en `include/config_pins.h`:
   ```cpp
   #define PIN_PIR_MOVIMIENTO   27
   #define PIN_I2S_SCK          18
   #define PIN_I2S_WS           32
   #define PIN_I2S_SD           26
   ```
2. Implementar compuerta temporal (cooldown / dead-time) en `tareaProcesamientoAudio`:
   - Bloquear nuevas ráfagas de audio por al menos 5 minutos (`INTERVALO_AUDIO_NORMAL_MS`) tras cada disparo.
3. Filtro de software en `leerDHTInterno`:
   - Si retorna `-200.0f`, retener el último valor válido en lugar de escribir anomalía al JSON.
4. Auto-sincronización NTP tras enlace WiFi exitoso.
