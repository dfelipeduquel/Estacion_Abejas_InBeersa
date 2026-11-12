# CALIBRACION — InBeersa Semillero Kilby UTP

## Uso rápido

### 1. Agregar environment en `platformio.ini`

Añade este bloque al archivo `platformio.ini` del proyecto raíz:

```ini
[env:calibracion]
platform = espressif32
board = esp32dev
framework = arduino
build_unflags = -std=gnu++11
build_flags = -std=gnu++17
monitor_speed = 115200
upload_speed = 115200
; Compilar SOLO este archivo, ignorar src/ principal
build_src_filter = -<*> +<../CALIBRACION/calibracion_sensores.cpp>
```

### 2. Compilar y subir

```powershell
# Compilar entorno de calibración
& "<RUTA_LOCAL>" run -e calibracion

# Subir a ESP32
& "<RUTA_LOCAL>" run -e calibracion --target upload

# Abrir monitor serial
& "<RUTA_LOCAL>" device monitor -e calibracion
```

### 3. Volver al firmware InBeersa principal

```powershell
& "<RUTA_LOCAL>" run -e esp32dev --target upload
```

---

## Hardware necesario para calibración lluvia

| Componente | Conexión |
|:--|:--|
| Placa lluvia — `D0` | GPIO 35 |
| Placa lluvia — `A0` | GPIO 34 |
| Resistencia pull-up `10kΩ` | Entre `D0` y `3.3V` del ESP32 |
| Placa lluvia — `VCC` | `3.3V` |
| Placa lluvia — `GND` | `GND` |

> **Sin la resistencia pull-up de 10kΩ, GPIO 35 flota y el modo digital no es confiable.**

---

## Modos disponibles

| Modo | Función |
|:--|:--|
| `[1]` Monitor Lluvia ADC | ADC bruto + EMA + nivel en tiempo real con barra ASCII |
| `[2]` Calibración Umbrales | Proceso guiado paso a paso para encontrar tus umbrales reales |
| `[3]` Monitor PIR | Conteo de eventos con delta-tiempo y detección de rebotes |
| `[4]` Prueba Combinada | Ambos sensores simultáneos — simula condiciones de campo |
| `[5]` Resumen de Sesión | Estadísticas: min/max ADC, total eventos PIR, umbrales activos |

---

## Resultado esperado del Modo 2 (calibración guiada)

Al finalizar, el sketch imprime los umbrales calculados listos para copiar a `sensors.cpp`:

```
╔══════════════════════════════════════════╗
║         UMBRALES CALIBRADOS              ║
╠══════════════════════════════════════════╣
║ Seco → Neblina  (UMBRAL_NEBLINA):  380   ║
║ Neblina → Ligera(UMBRAL_LIGERA):  1240   ║
║ Ligera → Intensa(UMBRAL_INTENSA): 2800   ║
╚══════════════════════════════════════════╝
```

Pega esos valores en `src/sensors.cpp` bajo la sección de calibración de lluvia analógica (v2.4).
