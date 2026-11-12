/**
 * =============================================================================
 * @file    calibracion_sensores.cpp
 * @brief   Sketch de Diagnóstico y Calibración — InBeersa Semillero Kilby UTP
 *          Sensores: PIR HC-SR501 (GPIO 27) + Lluvia Resistivo D0/A0 (GPIO 35/34)
 *
 * @version 1.0.0
 * @date    12/11/2026
 * @author  Daniel Felipe Duque Lizarralde | TerraBit | Kilby UTP
 *
 * INSTRUCCIONES DE USO:
 * -----------------------------------------------------------------------------
 * 1. En platformio.ini, cambiar la línea `src_dir` a la carpeta CALIBRACION:
 *       [env:calibracion]
 *       platform = espressif32
 *       board = esp32dev
 *       framework = arduino
 *       build_unflags = -std=gnu++11
 *       build_flags = -std=gnu++17
 *       monitor_speed = 115200
 *       upload_speed = 115200
 *       build_src_filter = -<*> +<../CALIBRACION/calibracion_sensores.cpp>
 *
 * 2. Compilar y subir con: pio run -e calibracion --target upload
 * 3. Abrir Monitor Serial a 115200 baud
 * 4. Seguir el menú interactivo via serial
 *
 * HARDWARE REQUERIDO:
 * -----------------------------------------------------------------------------
 * - PIR HC-SR501:    OUT → GPIO 27 | VCC → 5V | GND → GND
 * - Lluvia Digital:  D0  → GPIO 35 | VCC → 3.3V | GND → GND
 * - Lluvia Analógica:A0  → GPIO 34 | (mismo módulo, pin A0)
 * - NOTA: GPIO 35 sin pull-up interno → conectar 10kΩ entre D0 y 3.3V
 *
 * MODOS DISPONIBLES:
 * -----------------------------------------------------------------------------
 * [1] Monitor Lluvia ADC en Tiempo Real   — ver raw + EMA + nivel
 * [2] Calibración Umbral Lluvia           — encontrar thresholds personalizados
 * [3] Monitor PIR — Conteo y Timing       — validar ISR y debounce
 * [4] Prueba Combinada                    — ambos sensores simultáneos
 * [5] Reporte de Resumen de Sesión        — estadísticas acumuladas
 * =============================================================================
 */

#include <Arduino.h>

// =============================================================================
// CONFIGURACIÓN DE PINES (mirror de config_pins.h — standalone)
// =============================================================================
#define PIN_PIR_MOVIMIENTO   27    ///< GPIO 27 — PIR HC-SR501
#define PIN_LLUVIA_DIGITAL   35    ///< GPIO 35 — Lluvia D0 (requiere pull-up externo 10kΩ)
#define PIN_LLUVIA_ANALOGICO 34    ///< GPIO 34 — Lluvia A0 (ADC1_CH6, 12 bits)

// =============================================================================
// PARÁMETROS DE CALIBRACIÓN — AJUSTAR SEGÚN RESULTADOS
// =============================================================================
#define ALPHA_EMA            0.15f  ///< Coeficiente filtro exponencial (0.1=muy suave, 0.3=reactivo)
#define INTERVALO_MUESTREO_MS  200  ///< Periodo de lectura ADC en ms
#define DEBOUNCE_PIR_MS        300  ///< Tiempo mínimo entre flancos PIR para contar como evento distinto

// Umbrales ADC lluvia (0–4095) — INICIALES, ajustar con modo [2]
uint16_t UMBRAL_NEBLINA  = 400;
uint16_t UMBRAL_LIGERA   = 1500;
uint16_t UMBRAL_INTENSA  = 3000;

// =============================================================================
// VARIABLES GLOBALES DE SESIÓN
// =============================================================================
volatile uint32_t contadorPIR         = 0;   ///< Total de eventos PIR detectados
volatile uint32_t ultimoFlancoPIR_ms  = 0;   ///< Timestamp del último flanco (debounce)
volatile bool     nuevoPIR            = false;///< Flag para imprimir desde loop (no desde ISR)

float   emaLluvia      = 0.0f;   ///< Valor EMA acumulado del ADC de lluvia
uint16_t rawMin        = 4095;   ///< ADC mínimo registrado en la sesión
uint16_t rawMax        = 0;      ///< ADC máximo registrado en la sesión
uint32_t sesionInicio  = 0;      ///< millis() de inicio de sesión
uint32_t totalMuestras = 0;      ///< Contador de muestras ADC tomadas

uint8_t modoActivo = 0;          ///< Modo de operación actual (1–5)

// =============================================================================
// ISR — Interrupción PIR (IRAM_ATTR para ejecución en RAM)
// =============================================================================
void IRAM_ATTR isrPIR() {
    uint32_t ahora = millis();
    // Debounce por software: ignorar flancos más rápidos que DEBOUNCE_PIR_MS
    if ((ahora - ultimoFlancoPIR_ms) >= DEBOUNCE_PIR_MS) {
        contadorPIR++;
        ultimoFlancoPIR_ms = ahora;
        nuevoPIR = true;
    }
}

// =============================================================================
// FUNCIONES AUXILIARES
// =============================================================================

/**
 * @brief Convierte valor EMA a string de nivel de lluvia con umbrales personalizables.
 */
const char* nivelLluvia(uint16_t raw) {
    if (raw < UMBRAL_NEBLINA)  return "SECO      ";
    if (raw < UMBRAL_LIGERA)   return "NEBLINA   ";
    if (raw < UMBRAL_INTENSA)  return "LLV.LIGERA";
    return                            "LLV.INTENS";
}

/**
 * @brief Dibuja barra ASCII proporcional al valor ADC (0–4095).
 * @param valor   Valor ADC actual
 * @param ancho   Ancho máximo de la barra en caracteres
 */
void barraASCII(uint16_t valor, uint8_t ancho = 30) {
    uint8_t llena = map(valor, 0, 4095, 0, ancho);
    Serial.print('[');
    for (uint8_t i = 0; i < ancho; i++) {
        Serial.print(i < llena ? '#' : '-');
    }
    Serial.print(']');
}

/**
 * @brief Imprime el menú principal de selección de modo.
 */
void imprimirMenu() {
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════════╗"));
    Serial.println(F("║   INBEERSA — CALIBRADOR DE SENSORES v1.0         ║"));
    Serial.println(F("║   Semillero Kilby UTP | TerraBit                 ║"));
    Serial.println(F("╠══════════════════════════════════════════════════╣"));
    Serial.println(F("║  [1] Monitor Lluvia ADC (tiempo real)            ║"));
    Serial.println(F("║  [2] Calibración de Umbrales Lluvia              ║"));
    Serial.println(F("║  [3] Monitor PIR — Conteo y Timing               ║"));
    Serial.println(F("║  [4] Prueba Combinada (Lluvia + PIR)             ║"));
    Serial.println(F("║  [5] Reporte de Resumen de Sesión                ║"));
    Serial.println(F("║  [0] Volver al Menú                              ║"));
    Serial.println(F("╚══════════════════════════════════════════════════╝"));
    Serial.print(F("Selecciona modo > "));
}

// =============================================================================
// MODO 1 — Monitor Lluvia ADC en Tiempo Real
// =============================================================================
void modo1_monitorLluvia() {
    Serial.println(F("\n--- MODO 1: Monitor Lluvia ADC ---"));
    Serial.println(F("Presiona [0] + Enter para volver al menú\n"));
    Serial.println(F("RAW_ADC | EMA_ADC | DIGITAL | NIVEL       | BARRA"));
    Serial.println(F("--------|---------|---------|------------|------------------------------"));

    uint32_t ultimaLectura = 0;

    while (true) {
        // Verificar si el usuario quiere salir
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '0') { modoActivo = 0; return; }
        }

        uint32_t ahora = millis();
        if (ahora - ultimaLectura >= INTERVALO_MUESTREO_MS) {
            ultimaLectura = ahora;

            uint16_t rawADC   = analogRead(PIN_LLUVIA_ANALOGICO);
            uint8_t  digital  = digitalRead(PIN_LLUVIA_DIGITAL); // 0=lluvia, 1=seco con pull-up
            emaLluvia = ALPHA_EMA * rawADC + (1.0f - ALPHA_EMA) * emaLluvia;
            uint16_t emaInt = (uint16_t)emaLluvia;

            // Actualizar estadísticas de sesión
            if (rawADC < rawMin) rawMin = rawADC;
            if (rawADC > rawMax) rawMax = rawADC;
            totalMuestras++;

            // Imprimir fila de datos
            char buf[80];
            snprintf(buf, sizeof(buf), " %4u    |  %4u   |    %c    | %s | ",
                     rawADC, emaInt,
                     digital == LOW ? '1' : '0',  // 1=lluvia activa (pull-up → LOW=activo)
                     nivelLluvia(emaInt));
            Serial.print(buf);
            barraASCII(emaInt);
            Serial.println();
        }
    }
}

// =============================================================================
// MODO 2 — Calibración Interactiva de Umbrales
// =============================================================================
void modo2_calibracionUmbrales() {
    Serial.println(F("\n--- MODO 2: Calibración de Umbrales ---"));
    Serial.println(F("Sigue las instrucciones y observa los valores RAW+EMA.\n"));

    struct PasoCalib {
        const char* instruccion;
        const char* variable;
        uint16_t*   umbral;
    };

    PasoCalib pasos[] = {
        { "Deja la placa COMPLETAMENTE SECA. Presiona Enter cuando esté lista.", "NEBLINA",  &UMBRAL_NEBLINA  },
        { "Sopla sobre la placa (simula neblina/condensación). Presiona Enter.", "LIGERA",   &UMBRAL_LIGERA   },
        { "Moja ligeramente la placa (1-2 gotas). Presiona Enter.",              "INTENSA",  &UMBRAL_INTENSA  },
        { "Moja abundantemente (lluvia real). Presiona Enter.",                  nullptr,    nullptr          }
    };

    uint16_t lecturas[4] = {0, 0, 0, 0};

    for (uint8_t paso = 0; paso < 4; paso++) {
        Serial.print(F("\n[PASO "));
        Serial.print(paso + 1);
        Serial.print(F("/4] "));
        Serial.println(pasos[paso].instruccion);

        // Esperar Enter del usuario
        while (!Serial.available()) {
            uint16_t raw = analogRead(PIN_LLUVIA_ANALOGICO);
            emaLluvia = ALPHA_EMA * raw + (1.0f - ALPHA_EMA) * emaLluvia;
            Serial.print(F("\r  Live: RAW="));
            Serial.print(raw);
            Serial.print(F(" | EMA="));
            Serial.print((uint16_t)emaLluvia);
            Serial.print(F("    "));
            delay(200);
        }
        while (Serial.available()) Serial.read(); // Limpiar buffer

        // Capturar promedio de 10 muestras consecutivas
        uint32_t suma = 0;
        for (uint8_t i = 0; i < 10; i++) {
            suma += analogRead(PIN_LLUVIA_ANALOGICO);
            delay(50);
        }
        lecturas[paso] = suma / 10;

        Serial.print(F("\n  → Valor capturado para este estado: "));
        Serial.println(lecturas[paso]);
    }

    // Calcular umbrales como puntos medios entre estados consecutivos
    UMBRAL_NEBLINA = (lecturas[0] + lecturas[1]) / 2;
    UMBRAL_LIGERA  = (lecturas[1] + lecturas[2]) / 2;
    UMBRAL_INTENSA = (lecturas[2] + lecturas[3]) / 2;

    Serial.println(F("\n╔══════════════════════════════════════════╗"));
    Serial.println(F("║         UMBRALES CALIBRADOS              ║"));
    Serial.println(F("╠══════════════════════════════════════════╣"));
    Serial.print(  F("║ Seco → Neblina  (UMBRAL_NEBLINA):  "));
    Serial.print(UMBRAL_NEBLINA);  Serial.println(F("   ║"));
    Serial.print(  F("║ Neblina → Ligera(UMBRAL_LIGERA):   "));
    Serial.print(UMBRAL_LIGERA);   Serial.println(F("   ║"));
    Serial.print(  F("║ Ligera → Intensa(UMBRAL_INTENSA):  "));
    Serial.print(UMBRAL_INTENSA);  Serial.println(F("   ║"));
    Serial.println(F("╚══════════════════════════════════════════╝"));
    Serial.println(F("\n→ Copia estos valores a config_pins.h y sensors.cpp"));
    Serial.println(F("→ Presiona cualquier tecla para continuar..."));

    while (!Serial.available()) delay(100);
    while (Serial.available()) Serial.read();
    modoActivo = 0;
}

// =============================================================================
// MODO 3 — Monitor PIR: Conteo, Timing y Validación de ISR
// =============================================================================
void modo3_monitorPIR() {
    Serial.println(F("\n--- MODO 3: Monitor PIR ---"));
    Serial.println(F("Mueve la mano frente al sensor. Presiona [0]+Enter para volver.\n"));
    Serial.println(F("EVENTO | TOTAL | DELTA_T (ms) | ESTADO_GPIO | DEBOUNCE"));
    Serial.println(F("-------|-------|-------------|-------------|--------"));

    uint32_t contadorAnterior = 0;
    uint32_t tAnterior = millis();

    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '0') { modoActivo = 0; return; }
        }

        if (nuevoPIR) {
            nuevoPIR = false;
            uint32_t ahora = millis();
            uint32_t delta = ahora - tAnterior;
            tAnterior = ahora;

            char buf[80];
            snprintf(buf, sizeof(buf),
                     " #%4lu  |  %4lu |   %7lu   |   GPIO=%d    | %s",
                     (unsigned long)contadorPIR,
                     (unsigned long)(contadorPIR - contadorAnterior),
                     (unsigned long)delta,
                     digitalRead(PIN_PIR_MOVIMIENTO),
                     delta < DEBOUNCE_PIR_MS ? "REBOTE!" : "OK");
            Serial.println(buf);
            contadorAnterior = contadorPIR;
        }

        // Heartbeat cada 5 segundos si no hay eventos
        static uint32_t ultimoHeartbeat = 0;
        if (millis() - ultimoHeartbeat > 5000) {
            ultimoHeartbeat = millis();
            Serial.print(F("  [Esperando...] Total acumulado: "));
            Serial.print(contadorPIR);
            Serial.print(F(" | GPIO27="));
            Serial.println(digitalRead(PIN_PIR_MOVIMIENTO));
        }
    }
}

// =============================================================================
// MODO 4 — Prueba Combinada (Lluvia + PIR simultáneos)
// =============================================================================
void modo4_combinado() {
    Serial.println(F("\n--- MODO 4: Monitor Combinado Lluvia + PIR ---"));
    Serial.println(F("Presiona [0]+Enter para volver.\n"));

    uint32_t ultimaLectura = 0;

    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '0') { modoActivo = 0; return; }
        }

        // Imprimir evento PIR si llegó
        if (nuevoPIR) {
            nuevoPIR = false;
            Serial.print(F("  *** PIR EVENTO #"));
            Serial.print(contadorPIR);
            Serial.print(F(" — Tiempo desde boot: "));
            Serial.print(millis() / 1000);
            Serial.println(F("s ***"));
        }

        // Lectura periódica lluvia
        uint32_t ahora = millis();
        if (ahora - ultimaLectura >= INTERVALO_MUESTREO_MS) {
            ultimaLectura = ahora;

            uint16_t rawADC  = analogRead(PIN_LLUVIA_ANALOGICO);
            uint8_t  digital = digitalRead(PIN_LLUVIA_DIGITAL);
            emaLluvia = ALPHA_EMA * rawADC + (1.0f - ALPHA_EMA) * emaLluvia;
            uint16_t emaInt  = (uint16_t)emaLluvia;

            char buf[90];
            snprintf(buf, sizeof(buf),
                     "T=%5lus | LLUVIA raw=%4u EMA=%4u [%s] D0=%c | PIR_total=%lu",
                     (unsigned long)(ahora / 1000),
                     rawADC, emaInt,
                     nivelLluvia(emaInt),
                     digital == LOW ? '1' : '0',
                     (unsigned long)contadorPIR);
            Serial.println(buf);
        }
    }
}

// =============================================================================
// MODO 5 — Reporte de Resumen de Sesión
// =============================================================================
void modo5_resumen() {
    uint32_t duracionSeg = (millis() - sesionInicio) / 1000;

    Serial.println(F("\n╔══════════════════════════════════════════════╗"));
    Serial.println(F("║         REPORTE DE SESIÓN DE CALIBRACIÓN     ║"));
    Serial.println(F("╠══════════════════════════════════════════════╣"));

    char buf[64];
    snprintf(buf, sizeof(buf), "║  Duración sesión : %lu s (%lu min)          ",
             (unsigned long)duracionSeg, (unsigned long)(duracionSeg / 60));
    Serial.println(buf);

    snprintf(buf, sizeof(buf), "║  Muestras ADC    : %lu                      ",
             (unsigned long)totalMuestras);
    Serial.println(buf);

    snprintf(buf, sizeof(buf), "║  ADC Min/Max/EMA : %u / %u / %u               ",
             rawMin, rawMax, (uint16_t)emaLluvia);
    Serial.println(buf);

    snprintf(buf, sizeof(buf), "║  Eventos PIR     : %lu                      ",
             (unsigned long)contadorPIR);
    Serial.println(buf);

    Serial.println(F("╠══════════════════════════════════════════════╣"));
    Serial.println(F("║  UMBRALES ACTIVOS EN SESIÓN:                 ║"));

    snprintf(buf, sizeof(buf), "║  UMBRAL_NEBLINA  : %-4u                     ", UMBRAL_NEBLINA);
    Serial.println(buf);
    snprintf(buf, sizeof(buf), "║  UMBRAL_LIGERA   : %-4u                     ", UMBRAL_LIGERA);
    Serial.println(buf);
    snprintf(buf, sizeof(buf), "║  UMBRAL_INTENSA  : %-4u                     ", UMBRAL_INTENSA);
    Serial.println(buf);

    Serial.println(F("╚══════════════════════════════════════════════╝"));
    Serial.println(F("→ Presiona cualquier tecla para volver al menú..."));

    while (!Serial.available()) delay(100);
    while (Serial.available()) Serial.read();
    modoActivo = 0;
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    // Configurar pines
    pinMode(PIN_PIR_MOVIMIENTO,   INPUT_PULLDOWN);  // PIR: activo alto, pull-down por seguridad
    pinMode(PIN_LLUVIA_DIGITAL,   INPUT);            // GPIO 35: sin pull-up interno (pull-up EXTERNO 10kΩ)
    // GPIO 34 y 35 son solo entrada — no se configura pinMode para ADC

    // Adjuntar ISR del PIR en flanco de subida
    attachInterrupt(digitalPinToInterrupt(PIN_PIR_MOVIMIENTO), isrPIR, RISING);

    // Inicializar EMA con primera lectura real
    emaLluvia = (float)analogRead(PIN_LLUVIA_ANALOGICO);

    sesionInicio = millis();

    Serial.println(F("\n\n"));
    Serial.println(F("================================================="));
    Serial.println(F("  INBEERSA — CALIBRADOR DE SENSORES v1.0"));
    Serial.println(F("  Semillero Kilby UTP | Ecosistema TerraBit"));
    Serial.println(F("================================================="));
    Serial.print(F("  PIR GPIO: "));      Serial.println(PIN_PIR_MOVIMIENTO);
    Serial.print(F("  Lluvia D0 GPIO: ")); Serial.println(PIN_LLUVIA_DIGITAL);
    Serial.print(F("  Lluvia A0 GPIO: ")); Serial.println(PIN_LLUVIA_ANALOGICO);
    Serial.print(F("  ADC inicial: "));   Serial.println((uint16_t)emaLluvia);
    Serial.println(F("=================================================\n"));

    imprimirMenu();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    // Leer selección de modo desde Serial
    if (modoActivo == 0 && Serial.available()) {
        char c = Serial.read();
        while (Serial.available()) Serial.read(); // limpiar buffer

        switch (c) {
            case '1': modoActivo = 1; modo1_monitorLluvia();      imprimirMenu(); break;
            case '2': modoActivo = 2; modo2_calibracionUmbrales();imprimirMenu(); break;
            case '3': modoActivo = 3; modo3_monitorPIR();         imprimirMenu(); break;
            case '4': modoActivo = 4; modo4_combinado();          imprimirMenu(); break;
            case '5': modoActivo = 5; modo5_resumen();            imprimirMenu(); break;
            default:
                Serial.println(F("Opción no válida. Elige 1-5."));
                imprimirMenu();
                break;
        }
    }
}
