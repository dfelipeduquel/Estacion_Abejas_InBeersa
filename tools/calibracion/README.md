# Utilidad de Calibración de Sensores — InBeersa

Herramienta interactiva por puerto serie para validar y calibrar de forma aislada los sensores de **lluvia** (`GPIO 35` / `GPIO 34`) y **movimiento PIR** (`GPIO 27`).

## Compilación y Carga

El entorno `[env:calibracion]` ya está configurado en `platformio.ini`:

```bash
# Compilar y subir utilidad de calibración
pio run -e calibracion --target upload

# Abrir monitor serial (115200 baud)
pio device monitor -e calibracion
```

Para regresar al firmware principal de la estación:

```bash
pio run -e esp32dev --target upload
```

## Conexión de Hardware

| Sensor / Señal | Pin físico ESP32 | Notas |
|---|---|---|
| PIR HC-SR501 (`OUT`) | `GPIO 27` | `INPUT_PULLDOWN` interno |
| Lluvia digital (`D0`) | `GPIO 35` | Requiere resistencia pull-up externa de `10 kΩ` a `3.3V` |
| Lluvia analógica (`A0`) | `GPIO 34` | `ADC1_CH6` (12 bits, `0–4095`) |

> `GPIO 34–39` en ESP32 son entradas únicamente y no poseen resistencias pull-up/pull-down internas en silicio.

## Modos Disponibles en Monitor Serial

| Opción | Función |
|---|---|
| `[1]` | Monitor ADC de lluvia en tiempo real (`RAW`, `EMA`, estado digital y barra ASCII) |
| `[2]` | Calibración guiada paso a paso para calcular umbrales (`Seco`, `Neblina`, `Ligera`, `Intensa`) |
| `[3]` | Monitor de eventos PIR con conteo acumulado, `Δt` y detección de rebotes |
| `[4]` | Prueba combinada simultánea (Lluvia + PIR) |
| `[5]` | Reporte estadístico de la sesión |
