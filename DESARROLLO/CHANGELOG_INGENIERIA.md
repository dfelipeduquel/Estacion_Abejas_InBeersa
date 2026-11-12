# Bitácora Técnica de Ingeniería: Firmware InBeersa C++17

**Proyecto:** Estación Agrometeorológica y Bioacústica InBeersa (*Melipona eburnea* / Abejas Angelitas)  
**Semillero:** Kilby (Universidad Tecnológica de Pereira)  
**Autor & Líder Técnico:** Daniel Felipe Duque Lizarralde  

---

## [v2.0.0] - Migración Integral de MicroPython a C++17 Modular

### 1. Motivación y Diagnóstico del Código Legacy (MicroPython)
- **Fallo Crítico en ISR de PIR:** En `Interrupciones.py`, la detección de movimiento en la piquera invocaba operaciones I/O de audio I2S y montaje de tarjeta SD por 5 segundos dentro de la interrupción. Esto provocaba pánicos del kernel (*Interrupt WDT timeout*).
- **Lectura Falsa de MQ-135:** El sensor de gases estaba mapeado como pin digital `Pin.IN` en GPIO 34 en lugar de lectura analógica por ADC, anulando el sensado de calidad de aire en la colmena.
- **Bloqueos `utime.sleep()`:** Esperas síncronas bloqueaban el microcontrolador ante caídas de la red WiFi o pérdida de sincronismo con el broker HiveMQ.
- **Mapeo Eléctrico Desactualizado:** Se detectaron incongruencias entre los pines declarados en el script y las conexiones reales de la placa PCB medidas en laboratorio.

---

### 2. Mapeo de Acciones y Cambios Realizados

| Módulo Legacy (MicroPython) | Archivo C++17 Modular | Justificación y Mejora Técnica |
|---|---|---|
| *Configuraciones dispersas* | `include/config_pins.h` | Centralización unificada de pines según la hoja de continuidad de laboratorio de la PCB InBeersa. |
| *Variables globales mutables* | `include/globals.h` | Estructuras formales (`TelemetriaInBeersa`), tipos estrictos y primitivas FreeRTOS (semáforos y colas). |
| *Leds con `Pin.on()` dispersos* | `include/hmi.h` & `src/hmi.cpp` | Abstracción de LEDs de diagnóstico con manejo formal de la lógica inversa nativa de la placa. |
| `Funciones.py` (DHT & MQ-135) | `include/sensors.h` & `src/sensors.cpp` | Lectura de DHT22 interior/exterior protegida contra `NaN`, lectura analógica del MQ-135 con promedio de 8 muestras y filtro de media móvil exponencial (EMA, $\alpha=0.2$). |
| `Interrupciones.py` | `src/sensors.cpp` (ISRs) | Interrupciones de hardware ultraligeras (`IRAM_ATTR`), con debounce por software no bloqueante y sincronización con semáforos binarios FreeRTOS (`xSemaphoreGiveFromISR`). |
| `Funciones.py` (`FuncionAudio`) | `include/audio_i2s.h` & `src/audio_i2s.cpp` | Migración de audio I2S a **DMA por hardware** usando `driver/i2s.h` de ESP-IDF, ejecutado en una tarea dedicada anclada al Core 1 de FreeRTOS. Escritura de cabeceras WAV estándar de 32-bit PCM. |
| `sdcard.py` & montaje manual | `include/storage.h` & `src/storage.cpp` | Manejo del bus SPI para MicroSD con fallback transparente a **LittleFS** en caso de que la tarjeta falle o sea retirada en campo. |
| `Funciones.py` (WiFi & MQTT) | `include/comms.h` & `src/comms.cpp` | Conexión WiFi asíncrona, reconexión automática sin congelar el muestreo y serialización ultra rápida con `ArduinoJson`. |
| `main.py` | `src/main.cpp` | Orquestación en arquitectura basada en eventos, cero `delay()` en bucle principal, temporizadores no bloqueantes `millis()` y soporte para telemetría continua y ráfagas bioacústicas. |

---

## [v2.3.0] - Estabilización Post-Prueba 22h: Desacoplo de Pines, Cooldown Bioacústico, Filtro DHT y Auto-NTP

### 1. Diagnóstico Forense (Prueba de 22 Horas Continua)
- **Cruce de Pines SPI vs PIR:** En v2.2 `PIN_PIR_MOVIMIENTO` compartía `GPIO 14` con el reloj SPI `PIN_SD_SCK`. A 4 MHz, cada ciclo de escritura disparaba la interrupción `isrPIR` en bucle continuo generando 6,809 grabaciones de audio WAV innecesarias (2.18 GB).
- **Cruce de Pines I2S INMP441:** El micrófono digital presentaba pines invertidos en cabecera (`SD=27`, `WS=33`) y colisión con `PIN_LED_SD_MAL=32`.
- **Anomalías en Bus 1-Wire DHT22:** Se registraron 5 muestras atípicas (-200.0°C) por timeout de respuesta en 3,211 lecturas (99.84% confiabilidad).
- **Desfase Temporal del RTC DS1307:** Deriva horaria acumulativa por falta de auto-sincronización atómica.

---

### 2. Correcciones Implementadas en Silicio y Firmware

| Módulo / Archivo | Cambio Realizado | Justificación de Ingeniería |
|---|---|---|
| `include/config_pins.h` | `PIN_PIR_MOVIMIENTO = 27` | Desacopla la interrupción del PIR del reloj SPI SCK (GPIO 14), erradicando los falsos positivos por diafonía y actividad de bus. |
| `include/config_pins.h` | `PIN_I2S_SCK = 18`<br/>`PIN_I2S_WS = 32`<br/>`PIN_I2S_SD = 26` | Conciliación definitiva con el ruteo físico de la PCB InBeersa (J4) para captura PCM 32-bit @ 16 kHz. |
| `include/config_pins.h` | `PIN_LED_SD_MAL = 33` | Reubicación preventiva del LED de fallo MicroSD para liberar GPIO 32 al reloj Word Select (WS) de audio. |
| `include/config_pins.h` | `#define AUDIO_COOLDOWN_MS 300000` | Definición de constante de enfriamiento (5 min = 300 s) para la compuerta temporal bioacústica. |
| `src/audio_i2s.cpp` | Compuerta temporal en `tareaProcesamientoAudio` | Implementación de dead-time no bloqueante de 5 minutos mediante `millis()`. Descarta disparos repetitivos del PIR durante ventanas activas sin bloquear el Core 1. |
| `src/sensors.cpp` | Filtro de retención en `leerDHTInterno` y `leerDHTExterno` | Si el sensor retorna `NaN`, `-200.0°C` o valores fuera de rango físico, se retiene el último valor válido (con semilla segura $25^\circ\text{C}$ / $75\%$), protegiendo el cálculo de VPD y `/data.json`. |
| `src/comms.cpp` | Sincronización automática NTP $\to$ RTC | Al registrarse el flanco de conexión WiFi con `TerraBit`, se invoca inmediatamente `sincronizarRTCPorNTP()` hacia `pool.ntp.org` / `time.google.com` (UTC-5 Colombia). |
| `include/config_pins.h` | `DEFAULT_WIFI_POWER = WIFI_POWER_17dBm` | Atenuación suave de potencia RF WiFi (~50 mW) para suprimir picos transitorios de 500 mA y prevenir brownout durante escrituras MicroSD. |
| `src/main.cpp` | `INTERVALO_MUESTREO_MS = 300000` | Sincronización del ciclo biometeorológico a 5 minutos (288 muestras/día), alineando telemetría ambiental, Ubidots y rutina bioacústica. |
| `src/comms.cpp` | Dashboard Web Temático & Acciones de Élite | Interfaz embebida en memoria Flash con paleta oro miel/carbón, SoftAP concurrente (`InBeersa-Abejas-192.168.4.1`, pass: `CAMBIAR_CLAVE_AP`), mDNS `http://inbeersa.local`, streaming de `/descargar-datos`, prueba de audio I2S, purga segura de MicroSD y tabla viva de historial. |
| `src/storage.cpp` | `limpiarRegistrosSD()` y `obtenerUltimosRegistrosJSON()` | Rutinas de lectura segura de cola de registros JSON y reinicialización de almacenamiento. |
| `src/main.cpp` | Bump a v2.3.0 y actualización de banners | Sincronización de versiones de telemetría y diagnóstico por consola UART. |

---

## [v2.3.1] - Desacoplo de Disparo PIR, Bypass de Audio Manual Web y Formateo Decimal

### 1. Diagnóstico del Comportamiento en Caliente (Prueba en Vivo)
- **Monopolio del Semáforo de Audio por el PIR:** Cada detección de movimiento en piquera (`isrPIR`) liberaba el semáforo `semaforoGrabacionAudio`. Tras vencer el cooldown de 5 minutos, el PIR ganaba inmediatamente la ventana y registraba ráfagas `/Audios_PIR/`, bloqueando los audios periódicos de rutina y saturando el log con advertencias de enfriamiento.
- **Bloqueo del Botón de Audio en Dashboard Web:** Cuando el usuario pulsaba "Grabar Audio WAV", la tarea de audio lo rechazaba si el cooldown seguía activo por un pulso previo del PIR.
- **Falsos Positivos de Lluvia en Interior:** GPIO 35 es un pin de sólo entrada (*Input Only*) en el ESP32 y carece de resistencias pull-up/pull-down internas en silicio. Al no haber sensor conectado a J8, la línea quedaba flotando y disparaba la interrupción `isrLluvia` por ruido electromagnético.
- **Representación Numérica IEEE-754:** Los valores de telemetría se serializaban con colas binarias flotantes (`24.70000076`), afectando la presentación en el frontend y ocupando espacio innecesario en la MicroSD.

---

### 2. Mejoras de Ingeniería Aplicadas

| Módulo / Archivo | Cambio Realizado | Justificación Técnica |
|---|---|---|
| `include/config_pins.h` | `PIR_DISPARA_AUDIO = 0` | Desacopla la ISR del PIR de la tarea de audio. El PIR audita el tráfico de abejas (`contadorPIRAcumulado`) con total precisión, mientras que las ráfagas bioacústicas de rutina corren en cadencia limpia y periódica de 5 minutos. |
| `include/config_pins.h` | `HABILITAR_LLUVIA = 0` | Desactiva temporalmente el sensado de lluvia para evitar interrupciones por alta impedancia en GPIO 35 mientras no esté conectado el módulo físico. |
| `src/audio_i2s.cpp` | Bypass de Cooldown para Audio Manual | La bandera `banderaAudioManualWeb` permite que las peticiones del botón web graben inmediatamente en caliente sin esperar los 300 segundos del temporizador. |
| `src/sensors.cpp` | `INPUT_PULLDOWN` en GPIO 27 (PIR) | Inmunización eléctrica contra ruido parásito en la entrada del sensor PIR. |
| `src/comms.cpp` | Formateo Fijo `%.1f` / `%.2f` en JSON | `snprintf` directo para serializar telemetría limpia (`"TempI": 24.7`, `"VPD": 0.66`) en `/data.json` y Ubidots. |
| `src/comms.cpp` | Formateo con `toFixed()` en Tabla Web | Renderizado visual nítido de variables biofísicas en el Dashboard Web sin artefactos flotantes. |
| `src/comms.cpp` | Ruta `/favicon.ico` con HTTP 204 | Supresión de logs `[WebServer] request handler not found` en consola. |


