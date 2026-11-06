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

### 3. Matriz de Pines Conciliada (Hardware PCB InBeersa)
- **PIR Piquera:** GPIO 14 (J1)
- **Micrófono INMP441 (I2S):** SCK=GPIO 18, WS=GPIO 33, SD=GPIO 27 (J4)
- **DHT22 Interno (Nido):** GPIO 2 (J5)
- **DHT22 Externo (Ambiente):** GPIO 23 (J6)
- **Sensor Lluvia:** GPIO 35 (J8)
- **Sensor MQ-135 (Gas):** GPIO 34 (ADC1_CH6) (J3)
- **RTC DS1307 (I2C):** SDA=GPIO 5, SCL=GPIO 4 (Dirección 0x68)
- **MicroSD (SPI):** MOSI=GPIO 17, MISO=GPIO 16, SCK=GPIO 12, CS=GPIO 13 (J7)
- **LEDs Diagnóstico:** KeepAlive=GPIO 21, RTC_MAL=GPIO 19, DHTI_MAL=GPIO 22, DHTE_MAL=GPIO 25, SD_MAL=GPIO 32
