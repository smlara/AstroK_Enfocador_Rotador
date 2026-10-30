          #include <Arduino.h>
#include <AccelStepper.h>
#include <EEPROM.h>

#include "Config.h"
#include "ExternalDisplay.h"
#include "ManualEncoder.h"
#include "FocuserProtocol.h"
#include "TMC2226Manager.h"
#include "AlpacaServer.h"
#include "BleLink.h"
#include <esp_mac.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --- INSTANCIAS ---

Preferences preferences;

bool isRotator = false;
bool rotatorReverse = false;
float stepsPerDegreeGlobal = DEFAULT_ROTATOR_STEPS_PER_DEGREE; // Hasta leer NVS
// Micropasos que tiene puestos el driver ahora mismo. Se guarda para poder
// reescalar la posición si cambian: ver aplicarConfigMotor().
uint16_t microstepsAplicados = 0;
long maxSpeedGlobal = DEFAULT_FOCUSER_MAX_SPEED;
long accelerationGlobal = DEFAULT_FOCUSER_ACCELERATION;
long max_steps_global = DEFAULT_FOCUSER_MAX_STEPS;
long stallMinSpeedGlobal = DEFAULT_FOCUSER_STALL_MIN_SPEED;
uint8_t stallThresholdGlobal = DEFAULT_FOCUSER_STALL_THRESHOLD;

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);
long targetPos = 0;

ExternalDisplay oled;
FocuserProtocol protocolo(stepper, targetPos, isRotator, rotatorReverse);
TMC2226Manager tmcDriver;
AlpacaServer alpacaServer(stepper, targetPos, isRotator, max_steps_global, rotatorReverse, oled);

ManualEncoder manualControl(PIN_ENC_S1, PIN_ENC_S2, PIN_ENC_SW);

volatile bool motorStalled = false; // Flag para interrupción de colisión

void IRAM_ATTR stallInterrupt() {
    motorStalled = true;
}

// Atiende el protocolo serie durante las esperas bloqueantes del arranque
// (ver setTickCallback en AlpacaServer).
void atenderSerial() {
    protocolo.procesarSerial();
    protocolo.procesarBle();
}

// Lee de una sección de config la velocidad y la aceleración, las acota al
// rango útil y se las pasa al stepper. AccelStepper admite el cambio en
// marcha: recalcula la rampa desde la velocidad actual.
void aplicarVelocidades(JsonObject dev) {
    long vel = dev["max_speed"]    | (long)(isRotator ? DEFAULT_ROTATOR_MAX_SPEED : DEFAULT_FOCUSER_MAX_SPEED);
    long acc = dev["acceleration"] | (long)(isRotator ? DEFAULT_ROTATOR_ACCELERATION : DEFAULT_FOCUSER_ACCELERATION);

    maxSpeedGlobal     = constrain(vel, (long)MIN_MOTOR_SPEED, (long)MAX_MOTOR_SPEED);
    accelerationGlobal = constrain(acc, (long)MIN_MOTOR_ACCEL, (long)MAX_MOTOR_ACCEL);

    stepper.setMaxSpeed((float)maxSpeedGlobal);
    stepper.setAcceleration((float)accelerationGlobal);
}

// Sentido de giro, en los dos modos. Se invierte el pin DIR y no la posición,
// así que serie, ALPACA, encoder y pantalla siguen coherentes sin que ninguno
// tenga que saberlo. La fuente es la clave `reverse` de la config en NVS;
// rotatorReverse queda solo como espejo para el GET de ALPACA.
void aplicarSentido(JsonObject dev) {
    bool invertir = dev["reverse"] | (isRotator ? DEFAULT_ROTATOR_REVERSE : DEFAULT_FOCUSER_REVERSE);
    rotatorReverse = isRotator && invertir;
    stepper.setPinsInverted(invertir, false, false);
}

// La corriente de reposo se guardaba antes como un booleano `hold`, y la
// librería dejaba en parado la mitad de la de marcha. Las configuraciones
// guardadas con firmware anterior siguen teniendo ese booleano y ninguna clave
// nueva: sin esta traducción, actualizar el firmware cambiaría el par en
// reposo sin que nadie lo haya pedido.
// max_steps acotado a [1, MAX_FOCUSER_STEPS]. La web y el driver ya lo
// limitan, pero la config tambien entra por :CS# y por la API, y un valor
// fuera de rango dejaria al enfocador sin tope o con un tope imposible.
static long leerMaxSteps(JsonObject dev) {
    long v = dev["max_steps"] | (long)DEFAULT_FOCUSER_MAX_STEPS;
    return constrain(v, 1L, MAX_FOCUSER_STEPS);
}

static uint16_t leerCorrienteHold(JsonObject dev, uint16_t corrienteMarcha) {
    if (!dev["hold_current_ma"].isNull()) return dev["hold_current_ma"].as<uint16_t>();
    bool hold = dev["hold"] | (isRotator ? DEFAULT_ROTATOR_HOLD : DEFAULT_FOCUSER_HOLD);
    return hold ? (uint16_t)(corrienteMarcha / 2) : 0;
}

// Relee de NVS los parámetros del motor y se los aplica al driver sin
// reiniciar. Se invoca al guardar configuración (por :CS# o por la web), para
// que ajustar la sensibilidad del stall sea practicable: a base de reinicios
// no lo es.
void aplicarConfigMotor() {
    String configJson = preferences.getString("config", "");
    if (configJson == "") return;

    JsonDocument doc;
    if (deserializeJson(doc, configJson) != DeserializationError::Ok) return;

    JsonObject dev = isRotator ? doc["rotator"] : doc["focuser"];
    if (dev.isNull()) return;

    uint16_t corriente  = dev["current_ma"]      | (isRotator ? DEFAULT_ROTATOR_CURRENT_MA : DEFAULT_FOCUSER_CURRENT_MA);
    uint16_t micropasos = dev["microsteps"]      | (isRotator ? DEFAULT_ROTATOR_MICROSTEPS : DEFAULT_FOCUSER_MICROSTEPS);
    uint8_t  stall      = dev["stall_threshold"] | (isRotator ? DEFAULT_ROTATOR_STALL_THRESHOLD : DEFAULT_FOCUSER_STALL_THRESHOLD);
    uint16_t holdMa     = leerCorrienteHold(dev, corriente);

    stallMinSpeedGlobal = dev["stall_min_speed"] | (long)(isRotator ? DEFAULT_ROTATOR_STALL_MIN_SPEED : DEFAULT_FOCUSER_STALL_MIN_SPEED);

    if (isRotator) {
        // Los pasos por grado tambien en caliente: el driver ASCOM los relee
        // del equipo al conectar y convierte grados a pasos con ellos, asi que
        // si aqui siguiera el valor viejo hasta el proximo reinicio los angulos
        // saldrian escalados (pedir 180 y ver 183 en el display).
        float spd = dev["steps_per_degree"] | (float)DEFAULT_ROTATOR_STEPS_PER_DEGREE;
        if (spd > 0) {
            stepsPerDegreeGlobal = spd;
            max_steps_global = (long)(POSICION_CENTRAL + (360.0f * stepsPerDegreeGlobal));
        }
    } else {
        max_steps_global = leerMaxSteps(dev);
    }

    aplicarVelocidades(dev);
    aplicarSentido(dev);

    // Cambiar los micropasos cambia lo que vale un paso, no dónde está el eje.
    // Sin reescalar, el equipo se cree movido sin haberse movido: pasar de 32 a
    // 8 micropasos estando a 180 grados lo dejaría convencido de estar en 0.
    // OJO: max_steps del enfocador no se reescala solo, porque es un valor que
    // el usuario fija a mano y manda lo que haya guardado en esa misma pasada.
    if (micropasos > 0 && microstepsAplicados > 0 && micropasos != microstepsAplicados) {
        double factor = (double)micropasos / (double)microstepsAplicados;
        targetPos = (long)llround(targetPos * factor);
        stepper.setCurrentPosition((long)llround(stepper.currentPosition() * factor));
    }
    microstepsAplicados = micropasos;

    tmcDriver.setCurrent(corriente);
    tmcDriver.setMicrosteps(micropasos);
    tmcDriver.setHoldCurrent(holdMa);
    stallThresholdGlobal = stall;
    tmcDriver.setStallThreshold(stall); // usa stallMinSpeedGlobal, ya actualizado
}

long ultimaPosGuardada = -1;
unsigned long tiempoUltimoMovimiento = 0;

void setup() {
    // El buffer RX por defecto del CDC son 256 bytes, y el comando :CS# lleva
    // la configuración completa en base64 (~500 bytes): se desbordaba y el
    // comando no llegaba nunca entero. Debe ajustarse antes de begin().
    Serial.setRxBufferSize(2048);
    Serial.begin(115200);

    EEPROM.begin(512);
    
    // Inicializar pantalla primero para mostrar errores de boot
    oled.begin();

    if (!LittleFS.begin(true)) {
        oled.mostrarMensaje("ERR LFS: " + String(ERR_LFS_MOUNT), 2);
        delay(2000);
    }

    pinMode(PIN_MODE_SELECTOR, INPUT_PULLUP);
    delay(10); // Permitir que el pullup se estabilice
    isRotator = (digitalRead(PIN_MODE_SELECTOR) == LOW);

    // Inicializar Preferences para NVS
    preferences.begin("astrok", false);
    String configJson = preferences.getString("config", "");
    
    JsonDocument configDoc;
    
    if (configJson == "") {
        // Crear configuración por defecto si no existe en NVS
        JsonObject wifi = configDoc["wifi"].to<JsonObject>();
        wifi["enabled"] = DEFAULT_WIFI_ENABLED;
        wifi["timeout_ms"] = DEFAULT_WIFI_TIMEOUT_MS;
        wifi["retries"] = DEFAULT_WIFI_RETRIES;
        wifi["ap_name"] = DEFAULT_WIFI_AP_NAME;
        wifi["ap_password"] = DEFAULT_WIFI_AP_PASSWORD;
       // wifi["networks"].to<JsonArray>();

        configDoc["ble"]["enabled"] = DEFAULT_BLE_ENABLED;

        JsonObject focuser = configDoc["focuser"].to<JsonObject>();
        focuser["hold_current_ma"] = DEFAULT_FOCUSER_HOLD_CURRENT_MA;
        focuser["current_ma"] = DEFAULT_FOCUSER_CURRENT_MA;
        focuser["microsteps"] = DEFAULT_FOCUSER_MICROSTEPS;
        focuser["stall_threshold"] = DEFAULT_FOCUSER_STALL_THRESHOLD;
        focuser["max_steps"] = DEFAULT_FOCUSER_MAX_STEPS;
        focuser["fine_step"] = DEFAULT_FOCUSER_FINE_STEP;
        focuser["coarse_step"] = DEFAULT_FOCUSER_COARSE_STEP;
        focuser["max_speed"] = DEFAULT_FOCUSER_MAX_SPEED;
        focuser["acceleration"] = DEFAULT_FOCUSER_ACCELERATION;
        focuser["stall_min_speed"] = DEFAULT_FOCUSER_STALL_MIN_SPEED;
        focuser["reverse"] = DEFAULT_FOCUSER_REVERSE;

        JsonObject rotator = configDoc["rotator"].to<JsonObject>();
        rotator["hold_current_ma"] = DEFAULT_ROTATOR_HOLD_CURRENT_MA;
        rotator["current_ma"] = DEFAULT_ROTATOR_CURRENT_MA;
        rotator["microsteps"] = DEFAULT_ROTATOR_MICROSTEPS;
        rotator["stall_threshold"] = DEFAULT_ROTATOR_STALL_THRESHOLD;
        rotator["steps_per_degree"] = DEFAULT_ROTATOR_STEPS_PER_DEGREE;
        rotator["fine_step"] = DEFAULT_ROTATOR_FINE_STEP;
        rotator["coarse_step"] = DEFAULT_ROTATOR_COARSE_STEP;
        rotator["max_speed"] = DEFAULT_ROTATOR_MAX_SPEED;
        rotator["acceleration"] = DEFAULT_ROTATOR_ACCELERATION;
        rotator["stall_min_speed"] = DEFAULT_ROTATOR_STALL_MIN_SPEED;
        rotator["reverse"] = DEFAULT_ROTATOR_REVERSE;

        String outStr;
        serializeJson(configDoc, outStr);
        preferences.putString("config", outStr);
    } else {
        DeserializationError err = deserializeJson(configDoc, configJson);
        if (err) {
            oled.mostrarMensaje("ERR NVS: " + String(ERR_NVS_PARSE), 2);
            delay(2000);
        } else {
            // Configuración NVS cargada correctamente.
        }
    }

    JsonObject deviceConfig = isRotator ? configDoc["rotator"] : configDoc["focuser"];
    
    uint16_t tmc_current = deviceConfig["current_ma"] | (isRotator ? DEFAULT_ROTATOR_CURRENT_MA : DEFAULT_FOCUSER_CURRENT_MA);
    uint16_t tmc_hold_ma = leerCorrienteHold(deviceConfig, tmc_current);
    uint16_t tmc_microsteps = deviceConfig["microsteps"] | (isRotator ? DEFAULT_ROTATOR_MICROSTEPS : DEFAULT_FOCUSER_MICROSTEPS);
    uint8_t tmc_stall = deviceConfig["stall_threshold"] | (isRotator ? DEFAULT_ROTATOR_STALL_THRESHOLD : DEFAULT_FOCUSER_STALL_THRESHOLD);
    stallMinSpeedGlobal = deviceConfig["stall_min_speed"] | (long)(isRotator ? DEFAULT_ROTATOR_STALL_MIN_SPEED : DEFAULT_FOCUSER_STALL_MIN_SPEED);
    stallThresholdGlobal = tmc_stall;
    
    if (isRotator) {
        stepsPerDegreeGlobal = deviceConfig["steps_per_degree"] | DEFAULT_ROTATOR_STEPS_PER_DEGREE;
        max_steps_global = (POSICION_CENTRAL + (360L * stepsPerDegreeGlobal));
    } else {
        max_steps_global = leerMaxSteps(deviceConfig);
    }

    // Inicializar Driver TMC2226 por UART
    microstepsAplicados = tmc_microsteps;
    tmcDriver.begin(tmc_current, tmc_microsteps, tmc_hold_ma);
    tmcDriver.setStallThreshold(tmc_stall);
    tmcDriver.attachStallInterrupt(stallInterrupt);

    pinMode(PIN_ENA, OUTPUT);
    digitalWrite(PIN_ENA, LOW); 

    manualControl.begin();

    // Recuperar posición de EEPROM
    EEPROM.get(EEPROM_ADDR, targetPos);
    
    // Si es rotador, forzamos a POSICION_CENTRAL al iniciar
    // Si es enfocador, recuperamos la guardada (y la validamos)
    if (isRotator) {
        targetPos = POSICION_CENTRAL;
    } else {
        if (targetPos < 0 || targetPos > max_steps_global) {
            targetPos = 0; // Valor seguro si EEPROM está corrupta o vacía
        }
    }
    
    stepper.setCurrentPosition(targetPos);
    ultimaPosGuardada = targetPos;

    aplicarVelocidades(deviceConfig);
    aplicarSentido(deviceConfig);

    bool forceAP = false;
    if (digitalRead(PIN_ENC_SW) == LOW) {
        forceAP = true;
        oled.mostrarMensaje("FORZANDO AP...", 2);
        delay(1000);
    }

    // El arranque del WiFi bloquea varios segundos. Abrir el puerto USB
    // resetea el ESP32, así que un cliente serie (el driver ASCOM) que
    // conecte y pregunte de inmediato se comería ese silencio y daría
    // timeout: se atiende el protocolo durante esas esperas.
    // BLE va antes que la WiFi por lo mismo: se puede conectar y preguntar
    // mientras la WiFi arranca. El nombre lleva el mismo sufijo que el AP
    // por defecto (los 6 últimos hex de la MAC de estación), para que los dos
    // se reconozcan como el mismo equipo.
    // Sin la sección "ble" (configuración de un firmware anterior a 1.5),
    // encendido.
    if (configDoc["ble"]["enabled"] | DEFAULT_BLE_ENABLED) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char nombreBle[16];
        snprintf(nombreBle, sizeof(nombreBle), "AstroK_%02X%02X%02X", mac[3], mac[4], mac[5]);
        bleLink.begin(nombreBle, isRotator);
    }

    alpacaServer.setTickCallback(atenderSerial);
    protocolo.setConfigAppliedCallback(aplicarConfigMotor);
    alpacaServer.setConfigAppliedCallback(aplicarConfigMotor);
    alpacaServer.begin(forceAP);

    // Sin WiFi no hay IP que enseñar: ni la pantalla de red ni los 10 s de
    // espera para leerla.
    if (!alpacaServer.wifiActivo()) {
        motorStalled = false; // ver el final de setup()
        return;
    }

    String ipStr;
    if (WiFi.getMode() == WIFI_STA) {
        ipStr = WiFi.localIP().toString();
        oled.mostrarConexionWiFi("WiFi Connected!", "IP: " + ipStr);
    } else {
        ipStr = WiFi.softAPIP().toString();
        String apName = WiFi.softAPSSID();
        oled.mostrarConexionWiFi("AP: " + apName, "IP: " + ipStr);
    }
    
    unsigned long startWait = millis();
    while (millis() - startWait < 10000) {
        alpacaServer.loop(); // Procesar clientes web mientras espera
        atenderSerial(); // ...y también el protocolo serie y BLE

        // Salta la espera si se gira el encoder o se pulsa el botón, o si
        // hay que enseñar el PIN de un emparejamiento BLE (lo pinta loop()).
        if (manualControl.getUpdatedPosition(0, false) != 0 || digitalRead(PIN_ENC_SW) == LOW ||
            bleLink.pinPendiente() >= 0) {
            break;
        }
        delay(5);
    }

    // La configuración del driver y el propio cableado pueden dar algún flanco
    // espurio en DIAG durante el arranque. Si no se descarta aquí, el primer
    // loop() lo interpreta como colisión nada más encender.
    motorStalled = false;
}

void loop() {
    alpacaServer.loop();
    
    static unsigned long tiempoMensajeHasta = 0;

    // 1. Comunicación PC
    protocolo.procesarSerial();
    protocolo.procesarBle();

    // 2. Control Manual (Solo si está activado en el .ini)
    targetPos = manualControl.getUpdatedPosition(targetPos, stepper.distanceToGo() != 0);

    // El encoder no tenía límites (a diferencia de ALPACA, que sí los aplica
    // en /move y /sync). En Focuser se acota a [PASOS_MINIMOS, PASOS_MAXIMOS];
    // el Rotador ya gestiona su propio wrap de 0-360 más abajo.
    if (!isRotator) {
        if (targetPos > PASOS_MAXIMOS) targetPos = PASOS_MAXIMOS;
        if (targetPos < PASOS_MINIMOS) targetPos = PASOS_MINIMOS;
    }

    // Parada de emergencia: pulsación corta del botón mientras el motor se
    // movía (ver ManualEncoder). Clava el target en la posición actual para
    // que no siga persiguiendo un salto grande acumulado a mano (p.ej. varios
    // clics rápidos en x1000).
    if (manualControl.checkStopRequest()) {
        stepper.stop(); // Calcula una deceleración controlada con la aceleración configurada
        // OJO: sincronizar con targetPosition(), NO con currentPosition().
        // stop() ya ha calculado el punto exacto donde frenar en marcha; si
        // aquí se usara currentPosition() se le pediría parar de golpe desde
        // velocidad máxima en cero pasos, y un NEMA17 real sigue girando un
        // poco por inercia antes de asentarse (motorStalled sí quiere ese
        // frenazo brusco, por eso él usa currentPosition() a propósito).
        targetPos = stepper.targetPosition();
        oled.mostrarMensaje("STOP", 2);
        tiempoMensajeHasta = millis() + 1000;
    }

    // Verificar si se ha solicitado resetear la posición (pulsación larga)
    if (manualControl.checkResetRequest()) {
        if (isRotator) {
            // En modo rotador, la "posición 0" visual es (POSICION_CENTRAL)
            // para permitir movimiento en ambas direcciones.
            targetPos = POSICION_CENTRAL;
            stepper.setCurrentPosition(POSICION_CENTRAL);
            ultimaPosGuardada = POSICION_CENTRAL;
        } else {
            // En modo focuser estándar, 0 es 0
            targetPos = 0;
            stepper.setCurrentPosition(0);
            ultimaPosGuardada = 0;
        }
        oled.mostrarMensaje("RESET POS.", 2);
        tiempoMensajeHasta = millis() + 2000;
    }



    // 3. Movimiento Motor
    // Cuánto lleva el motor por encima de la velocidad a la que el driver
    // habilita StallGuard. Justo en ese cruce SG_RESULT todavía vale 0 y DIAG
    // se dispara solo, así que hace falta dejarlo asentar.
    // Banda de velocidad en la que la lectura de StallGuard es fiable, con
    // margen por los dos lados del punto en que el driver la habilita. El
    // guardián es simétrico a propósito: los falsos positivos aparecían tanto
    // al subir (rampa de aceleración) como al bajar (frenada del final), y
    // antes solo estaba cubierto el lado de subida.
    static unsigned long stallGuardActivoDesde = 0;
    float velocidadActual = fabs(stepper.speed());
    float velocidadFiable = (float)(stallMinSpeedGlobal * STALL_TRUST_FACTOR);
    if (velocidadActual >= velocidadFiable) {
        if (stallGuardActivoDesde == 0) stallGuardActivoDesde = millis();
    } else {
        stallGuardActivoDesde = 0;
    }

    // La condición se evalúa por NIVEL del pin, no por el flanco de la
    // interrupción. Con un choque sostenido DIAG sube una vez y se queda
    // arriba, así que solo hay un flanco: si ese flanco caía dentro del margen
    // de asentado se descartaba, y al no haber mas flancos la deteccion quedaba
    // muerta el resto del movimiento. Se veia clarisimo con la sensibilidad al
    // maximo, donde DIAG esta arriba desde el principio y no paraba nunca.
    //
    // La interrupcion se mantiene enganchada como aviso temprano, pero la
    // decision no depende de ella. digitalRead no pasa por UART, asi que
    // mirarlo en cada vuelta del bucle no cuesta nada.
    motorStalled = false; // se consume: quien manda es el nivel del pin

    static unsigned long stallPendienteDesde = 0;
    if (digitalRead(TMC_DIAG_PIN) == HIGH) {
        if (stallPendienteDesde == 0) stallPendienteDesde = millis();
    } else {
        stallPendienteDesde = 0; // se ha soltado: era un bache, no un choque
    }

    // Tiene que llevar levantado toda la ventana seguida. Eso es lo que separa
    // el bajon de un instante de las rampas de un choque de verdad.
    if (stallPendienteDesde != 0 &&
        (millis() - stallPendienteDesde) >= STALL_CONFIRM_MS) {

        stallPendienteDesde = 0;

        // Solo es una colisión de verdad si el motor lleva ya un rato moviéndose
        // por encima de esa velocidad: por debajo, la lectura de StallGuard es
        // ruido, y recién cruzado el umbral es directamente inválida.
        bool asentado = stallGuardActivoDesde != 0 &&
                        (millis() - stallGuardActivoDesde) >= STALL_SETTLE_MS;

        // Sensibilidad 0 = detección desactivada. Existe porque StallGuard no
        // es utilizable con micropasos altos: medido en placa, a 32 micropasos
        // SG_RESULT se queda en 2 incluso con el motor libre, así que no hay
        // margen para distinguir un choque.
        if (stallThresholdGlobal != 0 && stepper.distanceToGo() != 0 && asentado) {
            // Parada en seco, no stop(). stop() de AccelStepper no para: fija
            // el destino unos pasos POR DELANTE para frenar con rampa. Si
            // ademas se le pone targetPos en la posicion de ahora, que ya queda
            // por detras, decelera, se pasa de largo y vuelve marcha atras a
            // buscarla. Eso es lo que se veia al chocar: tardaba y retrocedia.
            //
            // setCurrentPosition deja _speed a 0 y destino igual a la posicion
            // actual, asi que se queda clavado donde estaba. En un choque es lo
            // que interesa: frenar con rampa serian ~180 pasos (casi 5 grados)
            // empujando contra el obstaculo.
            long posChoque = stepper.currentPosition();
            stepper.setCurrentPosition(posChoque);
            targetPos = posChoque;
            oled.mostrarMensaje("STALL!", 2);
            tiempoMensajeHasta = millis() + 2000;
        }
    }

    stepper.moveTo(targetPos);
    stepper.run();

    // 4. Guardar posición en EEPROM al detenerse tras un breve retraso
    if (stepper.distanceToGo() == 0 && targetPos != ultimaPosGuardada) {
        if (tiempoUltimoMovimiento == 0) {
            tiempoUltimoMovimiento = millis();
        } else if (millis() - tiempoUltimoMovimiento > SAVE_DELAY_MS) {
            EEPROM.put(EEPROM_ADDR, targetPos);
            EEPROM.commit();
            ultimaPosGuardada = targetPos;
            tiempoUltimoMovimiento = 0;
        }
    } else if (stepper.distanceToGo() != 0) {
        tiempoUltimoMovimiento = 0;
    }

    // 5. Pantalla
    static unsigned long ultimaActualizacionOled = 0;
    int progresoOta = protocolo.progresoActualizacion();
    if (progresoOta >= 0) {
        // Refresco lento: cada display() son ~25 ms de I2C que retrasan la
        // respuesta al trozo siguiente.
        if (millis() - ultimaActualizacionOled > 500) {
            oled.mostrarProgreso(progresoOta, protocolo.actualizandoWeb());
            ultimaActualizacionOled = millis();
        }
        return;
    }

    // PIN de emparejamiento BLE: se queda en pantalla mientras dure el
    // emparejamiento, y se quita en cuanto acaba (bien o mal).
    static long pinEnPantalla = -1;
    long pinBle = bleLink.pinPendiente();
    if (pinBle != pinEnPantalla) {
        pinEnPantalla = pinBle;
        if (pinBle >= 0) {
            char texto[12];
            snprintf(texto, sizeof(texto), "PIN %04ld", pinBle);
            oled.mostrarMensaje(texto, 2);
            tiempoMensajeHasta = millis() + 60000;
        } else {
            tiempoMensajeHasta = 0;
        }
    }

    if (millis() < tiempoMensajeHasta) {
         // No actualizamos la pantalla normal mientras dure el mensaje
         return;
    }

    if (millis() - ultimaActualizacionOled > 250) {
        int multState = (int)manualControl.getMultiplicadorState();
        oled.actualizar(stepper.currentPosition(), targetPos, multState, (stepper.distanceToGo() != 0));
        ultimaActualizacionOled = millis();
    }
}