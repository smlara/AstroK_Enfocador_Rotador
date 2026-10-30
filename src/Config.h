#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =================================================================
// Configuración General del Firmware
// =================================================================

extern bool isRotator;
extern bool rotatorReverse;

// Velocidad y aceleracion del motor, en micropasos/s y micropasos/s^2. El
// valor efectivo se lee de la configuracion (DEFAULT_*_MAX_SPEED y
// DEFAULT_*_ACCELERATION) y se ajusta desde la web sin reflashear: el techo
// real no lo pone el firmware sino la reductora, el par del motor y la carga
// colgada del rotador.
extern long maxSpeedGlobal;
extern long accelerationGlobal;

// Rango admitido al guardar. El limite alto no es del motor: AccelStepper
// genera los pasos por software desde loop(), compartiendo hilo con WiFi y
// pantalla, y por encima de ~10 kHz el ritmo se vuelve irregular.
#define MIN_MOTOR_SPEED 100
#define MAX_MOTOR_SPEED 10000
#define MIN_MOTOR_ACCEL 100
#define MAX_MOTOR_ACCEL 20000

// =================================================================
// Variante de motor 35BY48
// =================================================================
// De serie, enfocador y rotador llevan motores hibridos de 200 pasos por
// vuelta. Con MOTOR_35BY48, que lo define su entorno en platformio.ini, lleva
// el motor de un ZWO EAF: un 35BY48 de iman permanente (7,5 grados, 48 pasos
// por vuelta) con reduccion 120:1, alimentado a 12 V. Se usa sobre todo como
// enfocador, pero el modo lo sigue decidiendo el jumper, asi que se ajustan
// los dos. Lo que va aqui sustituye a los DEFAULT_FOCUSER_* y
// DEFAULT_ROTATOR_* de mas abajo, que estan protegidos con #ifndef. Como
// todos los defaults, solo entra en una placa con la NVS vacia o a la que le
// falte la clave.
#ifdef MOTOR_35BY48
// Comun a los dos modos.
//
// Bobina medida: 54 ohm. A 12 V no pasa de V/R = 222 mA, y lo que se
// configura es RMS: 150 mA son 212 mA de pico, justo por debajo de ese techo.
// Pedir mas no da mas par; solo deja al driver sin margen para regular, y el
// driver lo acaba marcando como carga abierta (OLA/OLB en :DS#), que es lo
// que se vio alimentandolo a 5 V con la corriente de serie del enfocador.
//
// Sin corriente en reposo. El iman permanente tiene par de retencion sin
// corriente, y para arrastrarlo desde la salida hay que vencerlo multiplicado
// por 120, cosa que ni el peso de la camara en el tubo ni la del rotador
// hacen. Al cortar la corriente el rotor cae al diente mas cercano, como
// mucho 3,75 grados en el motor, 0,03 en la salida, y no se acumula: al volver
// a moverse el driver lo devuelve a su micropaso. A cambio el motor no
// calienta mientras esta parado, que es casi siempre.
//
// StallGuard desactivado: a 150 mA el CS que queda (~4) no da lectura util,
// y tras 120:1 el motor no nota un tope hasta que ya ha empujado fuerte.

// Enfocador. 48 * 120 = 5760 pasos enteros por vuelta de la salida, los
// mismos que anuncia el EAF. A paso entero, el tope de MAX_FOCUSER_STEPS
// son 173 vueltas de recorrido; con 8 micropasos ya no hace falta mas
// resolucion y cada vuelta cuesta 8 veces mas pasos. La resolucion es la del
// propio EAF, y el TMC2209 interpola a 256 por dentro, asi que el giro no
// pierde suavidad.
// Probado en placa: media vuelta de la salida son 2880 pasos (120:1 real).
#define DEFAULT_FOCUSER_MICROSTEPS 1
#define DEFAULT_FOCUSER_CURRENT_MA 150
#define DEFAULT_FOCUSER_HOLD_CURRENT_MA 0
#define DEFAULT_FOCUSER_STALL_THRESHOLD 0
// Aqui manda el motor, no la reduccion: un iman permanente pierde par pronto
// con la velocidad. Probado en placa a 150 mA: 300 pasos enteros/s va limpio
// y vuelve a la marca; a 600 pierde pasos. 300 son una vuelta de la salida
// cada 19 s.
#define DEFAULT_FOCUSER_MAX_SPEED 300
#define DEFAULT_FOCUSER_ACCELERATION 150

// Rotador, con la salida de 120:1 moviendo el anillo directamente: 16 pasos
// enteros por grado. A 8 micropasos son 128 pasos/grado (0,008 grados), de
// sobra. La reduccion de 120:1 esta comprobada en modo enfocador; en el anillo
// falta dar una vuelta.
#define DEFAULT_ROTATOR_MICROSTEPS 8
#define DEFAULT_ROTATOR_STEPS_PER_DEGREE 128.0
#define DEFAULT_ROTATOR_CURRENT_MA 150
#define DEFAULT_ROTATOR_HOLD_CURRENT_MA 0
#define DEFAULT_ROTATOR_STALL_THRESHOLD 0
// 1600 pasos/s a 8 micropasos son 200 pasos enteros/s, 12,5 grados/s en el
// anillo. Tambien sin probar.
#define DEFAULT_ROTATOR_MAX_SPEED 1600
#define DEFAULT_ROTATOR_ACCELERATION 800
#endif

// StallGuard solo mide bien por encima de cierta velocidad: arrancando desde
// parado la carga leída es ruido y dispara colisiones falsas. Por debajo de
// este umbral se ignora, tanto en el driver (vía TCOOLTHRS) como al atender
// la interrupción.
//
// Es un compromiso y por eso se puede ajustar desde la pestaña de avanzados:
// demasiado bajo produce colisiones falsas al arrancar; demasiado alto y no
// detecta nada, porque un movimiento corto no llega a esa velocidad (con una
// aceleracion de 1000, un tramo de 1000 pasos apenas roza los 1000 pasos/s).
// Al subir la aceleracion este umbral se alcanza antes, asi que conviene
// revisarlo si se toca la velocidad.
// Va por modo porque el umbral esta en pasos/s, no en grados/s: el rotador
// trabaja a 8 micropasos y el enfocador a 32, asi que la misma velocidad
// angular son cuatro veces menos pasos por segundo en el rotador.
#define DEFAULT_FOCUSER_STALL_MIN_SPEED 400
#ifndef DEFAULT_ROTATOR_STALL_MIN_SPEED
#define DEFAULT_ROTATOR_STALL_MIN_SPEED 300
#endif
extern long stallMinSpeedGlobal;

// Sensibilidad activa del StallGuard. 0 desactiva la detección.
extern uint8_t stallThresholdGlobal;

// Al cruzar esa velocidad el driver habilita StallGuard, pero SG_RESULT aún
// vale 0 y levanta DIAG de inmediato. Se ignora la detección durante este
// margen tras entrar en la zona activa.
//
// Subido de 150 a 400 ms con el rotador montado y medido: 150 ms tapaban el
// instante del cruce pero no el resto de la rampa de aceleración, donde
// SG_RESULT todavía sube con la velocidad y se cuela por debajo del umbral.
#define STALL_SETTLE_MS 400

// Margen sobre stall_min_speed por debajo del cual no se hace caso a
// StallGuard. El driver la habilita al cruzar stall_min_speed (TCOOLTHRS),
// pero justo por encima de ese cruce SG_RESULT todavia no vale: medido en el
// rotador, las colisiones falsas del final caian a ~141 pasos/s con el umbral
// del driver en 100, o sea a 5-16 pasos del destino.
//
// Sustituye a un guardian anterior que calculaba la distancia de frenada con
// stepper.speed(). Esa es la velocidad PRETENDIDA por AccelStepper, no la
// real, y como el bucle no da para mas de ~420 pasos/s se pasaba de largo:
// dejaba sin vigilar el 59% del recorrido.
//
// Cuanto mas alto, mas ancha la zona ciega del final y menos falsos positivos.
// Medido con factor 3: los falsos del umbral 9 caian a 64-69 pasos del
// destino. Con 2 la banda es mas estrecha, asi que la zona ciega se queda en
// la mitad a cambio de apurar mas el margen.
#define STALL_TRUST_FACTOR 2

// Tras el primer aviso de DIAG se espera esto y se vuelve a mirar el pin antes
// de dar la colision por buena. SG_RESULT depende de la velocidad (datasheet
// 11.1: "start value depends on motor, velocity and operating current"), asi
// que en las rampas da bajones de un instante; un choque de verdad mantiene el
// valor por los suelos y DIAG arriba mientras el motor siga empujando.
//
// Leer el pin no pasa por UART, asi que confirmar no cuesta nada. El precio es
// que el motor sigue empujando contra el obstaculo durante esta ventana: a los
// ~420 pasos/s reales del rotador son unos 13 pasos, un tercio de grado.
#define STALL_CONFIRM_MS 30

// Reloj interno del TMC2209, necesario para convertir velocidad en TSTEP.
#define TMC_INTERNAL_CLOCK 12000000UL

extern float stepsPerDegreeGlobal;
#define PASOS_POR_GRADO stepsPerDegreeGlobal

extern long max_steps_global;
#define PASOS_MAXIMOS max_steps_global

// Mínimo permitido al mover el Focuser a mano con el encoder. Negativo (no 0)
// a propósito: si el "0" físico se ha quedado corto tras un ajuste mecánico,
// permite seguir bajando un poco para poder resetear la posición sin
// desmontar nada.
#define PASOS_MINIMOS -1000
#define POSICION_CENTRAL 0L

// Control manual con tres pulsadores normalmente abiertos a GND en lugar del
// encoder rotatorio, en los mismos pines: S1 = subir posición, S2 = bajar
// posición, SW = el pulsador de siempre (multiplicador / parada / reset largo
// / forzar AP). Aplica a Focuser y Rotador. Se elige en platformio.ini
// (custom_botones); el valor de aquí solo cubre compilar sin ese flag.
#ifndef CONTROL_CON_BOTONES
#define CONTROL_CON_BOTONES 1
#endif
// Mantener S1/S2 pulsado repite el paso: primero espera BOTON_REPETIR_INICIO_MS
// y luego repite cada BOTON_REPETIR_CADA_MS.
#define BOTON_ANTIRREBOTE_MS 30
#define BOTON_REPETIR_INICIO_MS 500
#define BOTON_REPETIR_CADA_MS 150

// =================================================================
// Configuración del Driver TMC2226
// =================================================================

#define TMC_SERIAL_PORT Serial1
#define TMC_R_SENSE 0.11f
#define TMC_ADDRESS 0

// =================================================================
// Valores por defecto de la Configuración (JSON / NVS)
// =================================================================

// Focuser defaults
#define DEFAULT_FOCUSER_HOLD true
// 400 mA, no 800: hasta que se desactivo el escalado analogico
// (I_scale_analog) la corriente real era la pedida por UART multiplicada por
// VREF/2.5V, y con el potenciometro de fabrica de BTT (~1.26V medidos) esos
// 800 mA entregaban en realidad ~400. Ahora que el potenciometro ya no influye,
// pedir 800 doblaria la corriente de golpe respecto a lo que el equipo lleva
// funcionando, con los umbrales de stall calibrados para la corriente vieja.
#ifndef DEFAULT_FOCUSER_CURRENT_MA
#define DEFAULT_FOCUSER_CURRENT_MA 400
#endif
// Corriente que se mantiene con el motor parado, en mA RMS. Es la que de
// verdad manda en el calor: el equipo esta quieto casi toda la sesion, y la
// disipacion en reposo va con el cuadrado de este numero. 0 = sin par en
// reposo, el eje queda suelto. Bajarla demasiado deja que el tren de imagen se
// venza por su propio peso.
#ifndef DEFAULT_FOCUSER_HOLD_CURRENT_MA
#define DEFAULT_FOCUSER_HOLD_CURRENT_MA 200
#endif
// 4 y no 32: con el motor en el mando reducido del enfocador sobra
// resolución, y el bucle no pasa de ~490 pasos/s, así que a 32 el movimiento
// era desesperadamente lento y el recorrido salía en cifras enormes.
#ifndef DEFAULT_FOCUSER_MICROSTEPS
#define DEFAULT_FOCUSER_MICROSTEPS 4
#endif
// Invierte el pin DIR: para cuando el cable del motor viene hecho y el
// enfocador sale al revés. Se invierte en el pin y no en la posición, así que
// todos los caminos (serie, ALPACA, encoder, web) siguen coherentes.
// Ver aplicarSentido() en main.cpp, que sirve a los dos modos.
#define DEFAULT_FOCUSER_REVERSE false
#ifndef DEFAULT_FOCUSER_STALL_THRESHOLD
#define DEFAULT_FOCUSER_STALL_THRESHOLD 100
#endif
// Tope absoluto del recorrido del enfocador, y valor de fabrica: el limite
// real lo pone el usuario con max_steps, a la medida de su enfocador. Antes
// era 65535 porque la posicion viajaba en 4 cifras hex; ahora el
// protocolo serie manda las que hagan falta (ver enviarRespuestaHex). 999999
// son 6 cifras, lo que cabe en la pantalla y en los campos de la web.
#define MAX_FOCUSER_STEPS 999999L
#define DEFAULT_FOCUSER_MAX_STEPS 999999
#define DEFAULT_FOCUSER_FINE_STEP 100
#define DEFAULT_FOCUSER_COARSE_STEP 1000
#ifndef DEFAULT_FOCUSER_MAX_SPEED
#define DEFAULT_FOCUSER_MAX_SPEED 2500
#endif
#ifndef DEFAULT_FOCUSER_ACCELERATION
#define DEFAULT_FOCUSER_ACCELERATION 1000
#endif

// Rotator defaults
#define DEFAULT_ROTATOR_HOLD true
// Medido en el rotador montado: 800 mA mueven con holgura y calientan poco,
// porque lo que esta puesto casi todo el tiempo es la corriente de reposo.
#ifndef DEFAULT_ROTATOR_CURRENT_MA
#define DEFAULT_ROTATOR_CURRENT_MA 800
#endif
// Ver la nota de DEFAULT_FOCUSER_HOLD_CURRENT_MA.
#ifndef DEFAULT_ROTATOR_HOLD_CURRENT_MA
#define DEFAULT_ROTATOR_HOLD_CURRENT_MA 200
#endif
// 8 micropasos, no 32: el micropaso son 1/37.75 = 0.027 grados, cuatro veces
// mas fino que la tolerancia de 0.1 grados con la que se trabaja, y por debajo
// de 1/8 el limite ya no es el micropaso sino el backlash de la reductora y la
// friccion. A cambio, cada grado son cuatro veces menos pasos que generar, que
// es lo que permite girar rapido. El TMC2209 interpola a 256 internamente
// (MicroPlyer), asi que la suavidad de giro no se pierde.
#ifndef DEFAULT_ROTATOR_MICROSTEPS
#define DEFAULT_ROTATOR_MICROSTEPS 8
#endif
// Igual que DEFAULT_FOCUSER_REVERSE: invierte el pin DIR. Es la propiedad
// Reverse de ASCOM/ALPACA, guardada en NVS para que sobreviva al reinicio.
#define DEFAULT_ROTATOR_REVERSE false
// Ajustado a mano en el rotador montado, a 800 mA y con la velocidad minima en
// 300: detecta el tope y no salta sola. Provisional a falta de mas pruebas,
// pero es la primera combinacion que funciona por los dos lados.
//
// Los tres numeros van juntos y no se pueden mover por separado. La velocidad
// minima en 300 (frente a los 100 de antes) es lo que permite subir tanto la
// sensibilidad: sube el suelo de la banda en la que se hace caso a StallGuard,
// y ahi arriba la lectura es mucho mas estable. Cambiar corriente o micropasos
// obliga a repasar los tres.
//
// Cuanto mas alto el umbral, con menos fuerza salta, que es lo que hace falta
// para que un choque de verdad se note.
//
// A 32 micropasos no habia forma (SG_RESULT se quedaba en 2 con el motor
// libre), asi que bajar a 8 no solo ha dado velocidad: ha devuelto el
// StallGuard al terreno util. Ojo: estos numeros valen para esta corriente;
// cambiar current_ma obliga a repasar la sensibilidad.
#ifndef DEFAULT_ROTATOR_STALL_THRESHOLD
#define DEFAULT_ROTATOR_STALL_THRESHOLD 16
#endif
// Medido en placa: 151 pasos/grado a 32 micropasos. A 8 micropasos son
// exactamente una cuarta parte, asi que la calibracion se traslada sin volver
// a medir (y 37.75 es exacto en binario, no arrastra redondeo).
#ifndef DEFAULT_ROTATOR_STEPS_PER_DEGREE
#define DEFAULT_ROTATOR_STEPS_PER_DEGREE 37.75
#endif
#define DEFAULT_ROTATOR_FINE_STEP 1.0
#define DEFAULT_ROTATOR_COARSE_STEP 10.0
// El rotador da vueltas enteras (360 grados son 13590 micropasos a 37.75 por
// grado), asi que va mas suelto de serie que el enfocador, cuyos recorridos son
// cortos. 2000 pasos/s son 53 grados/s: el doble de lo que daba la
// configuracion anterior a 32 micropasos, y aun lejos del punto donde el motor
// se queda sin par.
#ifndef DEFAULT_ROTATOR_MAX_SPEED
#define DEFAULT_ROTATOR_MAX_SPEED 2000
#endif
#ifndef DEFAULT_ROTATOR_ACCELERATION
#define DEFAULT_ROTATOR_ACCELERATION 1000
#endif

// WiFi defaults
// false: ni radio ni servidores (ALPACA, web); solo USB. Arranca sin la espera
// del WiFi. Pulsar el encoder al arrancar fuerza el AP igualmente.
#define DEFAULT_WIFI_ENABLED true
#define DEFAULT_WIFI_TIMEOUT_MS 1000
#define DEFAULT_WIFI_RETRIES 10
#define DEFAULT_WIFI_AP_NAME ""
#define DEFAULT_WIFI_AP_PASSWORD "12345678"

// BLE defaults
// false: no se anuncia ni acepta conexiones por Bluetooth (sección "ble" de
// la config; si falta, encendido).
#define DEFAULT_BLE_ENABLED true

// =================================================================
// Códigos de Error (Mostrados en OLED)
// =================================================================
// ERR 1: Fallo al montar la partición LittleFS (Sistema de archivos web)
#define ERR_LFS_MOUNT 1

// ERR 2: Fallo al leer o parsear el JSON de configuración en NVS
#define ERR_NVS_PARSE 2

// =================================================================
// Configuración de Memoria y Sistema
// =================================================================

#define EEPROM_ADDR 0
#define SAVE_DELAY_MS 2000
#define SERIAL_BAUD 115200

// =================================================================
// Asignación de Pines (C3 vs S3)
// =================================================================

#if defined(ARDUINO_ESP32_C3)
  // --- Mapeo del PCB con ESP32-C3 SuperMini ---
  // GPIO2, GPIO8 y GPIO9 son pines de strapping del C3: GPIO2 se deja sin
  // conectar y SDA/SCL llevan pull-up en placa, que es lo que necesitan para
  // arrancar. Nada que pueda estar a nivel bajo en el reset va en esos tres.
  #define PIN_STEP 5
  #define PIN_DIR 6
  #define PIN_ENA 7

  #define TMC_UART_TX 21
  #define TMC_UART_RX 20
  #define TMC_DIAG_PIN 10

  #define PIN_SDA 8
  #define PIN_SCL 9

  // El cableado real de este encoder llega desplazado una posición respecto
  // al diseño (verificado con el diagnóstico de checkPCB: al girar solo
  // cambian GPIO1/GPIO3 en cuadratura, y el pulsador real está en GPIO0).
  // CLK->GPIO1, DT->GPIO3, SW->GPIO0. Se ajusta aquí en vez de recablear.
  #define PIN_ENC_S1 1
  #define PIN_ENC_S2 3
  #define PIN_ENC_SW 0
  #define PIN_MODE_SELECTOR 4

#else
  // --- Mapeo del PCB AstroRoEn V1.2 (módulo ESP32-S3 de 9 pines por lado) ---
  // Verificado contra el cobre de la placa.
  // Columna izquierda del módulo (USB arriba): TX RX IO1 IO2 IO3 IO4 IO5 IO6 IO7
  // Columna derecha:                          5V GND 3V3 IO13 IO12 IO11 IO10 IO9 IO8
  // Libres para ampliaciones: IO1, IO12, IO13.
  #define PIN_STEP 3
  #define PIN_DIR 4
  #define PIN_ENA 5

  // IO43/IO44 son los pines rotulados TX/RX del módulo (UART0 de la ROM).
  // La consola sigue yendo por USB gracias a ARDUINO_USB_CDC_ON_BOOT=1.
  #define TMC_UART_TX 43
  #define TMC_UART_RX 44
  #define TMC_DIAG_PIN 2

  #define PIN_SDA 6
  #define PIN_SCL 7

  #define PIN_ENC_S1 8
  #define PIN_ENC_S2 9
  #define PIN_ENC_SW 10
  #define PIN_MODE_SELECTOR 11
#endif

#endif // CONFIG_H