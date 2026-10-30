#include "FocuserProtocol.h"
#include <EEPROM.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "Config.h"
#include "TMC2226Manager.h"
#include <Update.h>
#include <LittleFS.h>
#include "BleLink.h"

extern Preferences preferences;
extern TMC2226Manager tmcDriver;

// Los valores de configuración (SSID, contraseñas) pueden contener ':' o '#',
// que son los delimitadores del protocolo. Por eso el JSON viaja en base64:
// así el marcado nunca se rompe por el contenido.
static String b64Encode(const String& plano) {
    size_t necesario = 0;
    mbedtls_base64_encode(nullptr, 0, &necesario,
                          (const unsigned char*)plano.c_str(), plano.length());
    if (necesario == 0) return "";

    char* buf = (char*)malloc(necesario + 1);
    if (!buf) return "";

    size_t escritos = 0;
    int r = mbedtls_base64_encode((unsigned char*)buf, necesario, &escritos,
                                  (const unsigned char*)plano.c_str(), plano.length());
    String salida;
    if (r == 0) {
        buf[escritos] = '\0';
        salida = String(buf);
    }
    free(buf);
    return salida;
}

static bool b64Decode(const String& codificado, String& salida) {
    size_t necesario = 0;
    // Primera llamada: devuelve el tamaño necesario en 'necesario'.
    mbedtls_base64_decode(nullptr, 0, &necesario,
                          (const unsigned char*)codificado.c_str(), codificado.length());
    if (necesario == 0) return false;

    char* buf = (char*)malloc(necesario + 1);
    if (!buf) return false;

    size_t escritos = 0;
    int r = mbedtls_base64_decode((unsigned char*)buf, necesario, &escritos,
                                  (const unsigned char*)codificado.c_str(), codificado.length());
    if (r == 0) {
        buf[escritos] = '\0';
        salida = String(buf);
    }
    free(buf);
    return r == 0;
}

FocuserProtocol::FocuserProtocol(AccelStepper& stepper, long& targetPos, bool& isRotatorRef, bool& rotatorReverseRef)
    : _stepper(stepper), _targetPos(targetPos), _isRotator(isRotatorRef), _rotatorReverse(rotatorReverseRef),
      _onConfigApplied(nullptr), _out(&Serial) {
    _buffer.reserve(16); // Evita fragmentación de memoria
    _bufferBle.reserve(16);
}

void FocuserProtocol::setConfigAppliedCallback(void (*cb)()) {
    _onConfigApplied = cb;
}

void FocuserProtocol::procesarSerial() {
    procesarCanal(Serial, _buffer);
}

void FocuserProtocol::procesarBle() {
    procesarCanal(bleLink, _bufferBle);
}

// Cada canal tiene su buffer: un comando a medias por USB no se mezcla con
// uno que llegue por BLE. La respuesta sale por el canal que trajo el comando.
void FocuserProtocol::procesarCanal(Stream& canal, String& buffer) {
    while (canal.available() > 0) {
        char c = (char)canal.read();
        if (c == ':') {
            buffer = "";
        } else if (c == '#') {
            _out = &canal;
            ejecutarComando(buffer);
            buffer = "";
        } else {
            buffer += c;
        }
    }
}

void FocuserProtocol::ejecutarComando(String cmd) {
    // 1. Limpiar prefijos numéricos (IDs de canal)
    while (cmd.length() > 0 && isDigit(cmd[0])) {
        cmd.remove(0, 1);
    }
    
    // Commands can be just 2 chars (e.g., GT, GI, GV) or contain params
    if (cmd.length() < 2) return;

    // Solo el código de 2 letras se normaliza a mayúsculas. Los parámetros NO:
    // el base64 de :CS# distingue mayúsculas de minúsculas y pasarlo entero a
    // mayúsculas lo corrompía. Los parámetros hex no se ven
    // afectados porque strtol() acepta ambas cajas.
    String codigo = cmd.substring(0, 2);
    codigo.toUpperCase();
    String params = (cmd.length() > 2) ? cmd.substring(2) : "";

    // Por BLE, hasta dar el PIN solo se atiende lo necesario para darlo.
    bool porBle = (_out == &bleLink);
    if (porBle && !bleLink.autenticado() && codigo != "BQ" && codigo != "BA") {
        _out->print("ER:PIN#");
        return;
    }

    // 2. Máquina de estados / Despachador (Switch por primera letra)
    switch (codigo[0]) {
        case 'G': // GETTERS
            // :GV# es de dos digitos, asi que aqui va "14" y no
            // "1.4": hay clientes que parsean exactamente dos caracteres. Para
            // la version completa y legible esta :DV#, que usa la app propia.
            // Mantener sincronizado con custom_version de platformio.ini.
            if (codigo == "GV") { _out->print("15#"); } // version 1.5
            else if (codigo == "GP") {
                long pos = _stepper.currentPosition();
                if (_isRotator) {
                    // Mapeo especial del rotador: 32768 es el punto 0 lógico
                    long posLogica = (pos - POSICION_CENTRAL) + 32768L;
                    enviarRespuestaHex(posLogica);
                } else {
                    enviarRespuestaHex(pos);
                }
            }
            else if (codigo == "GN") {
                long pos = _targetPos;
                if (_isRotator) {
                    long posLogica = (pos - POSICION_CENTRAL) + 32768L;
                    enviarRespuestaHex(posLogica);
                } else {
                    enviarRespuestaHex(pos);
                }
            }
            else if (codigo == "GI") { _out->print(_stepper.distanceToGo() != 0 ? "01#" : "00#"); }
            else if (codigo == "GD") { _out->print("02#"); } // Motor Delay (Fijo para engañar a ASCOM)
            else if (codigo == "GH") { _out->print("00#"); } // Half step: No
            // El dispositivo no lleva sensor de temperatura. Se responde a GT y GC
            // para no dejar colgado al cliente, que espera 4 dígitos hex siempre.
            else if (codigo == "GT") { enviarRespuestaHex(0); }
            else if (codigo == "GC") { enviarRespuestaHex(0); }
            break;

        case 'S': // SETTERS
            if (codigo == "SN") {
                // Validación básica: Si params está vacío, strtol devuelve 0, lo cual podría ser indeseado si fue un error de transmisión
                if (params.length() > 0) {
                    long nuevaPos = strtol(params.c_str(), NULL, 16);
                    
                    if (_isRotator) {
                        // El cliente envía una posición absoluta desplazada respecto a 32768.
                        // 32768 en el protocolo = 0 en nuestro sistema interno.
                        long targetLogicalSteps = nuevaPos - 32768L;
                        
                        long pasosPorVuelta = (long)round(360.0 * PASOS_POR_GRADO);
                        
                        // Normalización circular de la posición del rotador
                        targetLogicalSteps %= pasosPorVuelta;
                        if (targetLogicalSteps < 0) targetLogicalSteps += pasosPorVuelta;

                        _targetPos = POSICION_CENTRAL + targetLogicalSteps;
                    } else {
                        _targetPos = nuevaPos;
                    }
                }
            }
            else if (codigo == "SP") {
                if (params.length() > 0) {
                    long p = strtol(params.c_str(), NULL, 16);
                    if (_isRotator) {
                        // Sync: el cliente envía 'p' (donde 32768 es 0 deg)
                        long targetLogicalSteps = p - 32768L;
                        
                        long pasosPorVuelta = (long)round(360.0 * PASOS_POR_GRADO);
                        targetLogicalSteps %= pasosPorVuelta;
                        if (targetLogicalSteps < 0) targetLogicalSteps += pasosPorVuelta;
                        
                        long physicalTarget = POSICION_CENTRAL + targetLogicalSteps;
                        _stepper.setCurrentPosition(physicalTarget);
                        _targetPos = physicalTarget;
                    } else {
                        _stepper.setCurrentPosition(p);
                        _targetPos = p;
                    }
                }
            }
            else if (codigo == "SD") { /* Aquí podrías ajustar la velocidad si quisieras */ }
            // Coeficiente de compensación térmica: se acepta y se descarta, no hay sensor.
            else if (codigo == "SC") { }
            break;

        case 'F': // ACTIONS
            if (codigo == "FQ") { // Parada de emergencia
                _stepper.stop();
                _targetPos = _stepper.currentPosition();
            }
            else if (codigo == "FG") { /* El movimiento ya lo gestiona el loop */ }
            break;

        // --- Extensiones AstroK ---
        // Existen porque la configuración de red solo era accesible por HTTP, y
        // eso no sirve precisamente cuando la WiFi es lo que hay que arreglar.
        // Las usa el diálogo de configuración del driver ASCOM por USB.
        case 'C': // CONFIG (mismo JSON que la API web /api/setup/config)
                  // :CG# leer, :CS<b64># guardar, :CR# valores de fábrica.
            if (codigo == "CG") {
                String cfg = preferences.getString("config", "{}");
                _out->print(b64Encode(cfg));
                _out->print("#");
            }
            else if (codigo == "CS") {
                String json;
                bool ok = params.length() > 0 && b64Decode(params, json);
                if (ok) {
                    // Se valida antes de guardar: un JSON roto en NVS deja el
                    // equipo sin configuración utilizable en el siguiente arranque.
                    JsonDocument doc;
                    ok = (deserializeJson(doc, json) == DeserializationError::Ok);
                    if (ok) {
                        preferences.putString("config", json);
                        // Los parámetros del motor se aplican en caliente; los
                        // de WiFi siguen necesitando reinicio (:WR#).
                        if (_onConfigApplied) _onConfigApplied();
                    }
                }
                _out->print(ok ? "OK#" : "ER#");
            }
            else if (codigo == "CR") {
                // Valores de fábrica: se borra la clave entera y el siguiente
                // arranque la recrea con los defectos. Existe por USB, y no
                // solo en la web, porque el caso en el que hace falta resetear
                // suele ser justo aquel en el que no hay forma de llegar por
                // red al equipo.
                preferences.remove("config");
                // El PIN de BLE también: tras un reset de fábrica, el primero
                // que conecte por BLE elige uno nuevo.
                bleLink.borrarPin();
                _out->print("OK#");
                _out->flush();
                delay(200);
                ESP.restart();
            }
            break;

        case 'D': // DIAGNÓSTICO del driver / modo del equipo
            if (codigo == "DS") {
                _out->print(tmcDriver.getDiagnostics());
                _out->print("#");
            }
            else if (codigo == "DV") {
                // Version completa del firmware, sin el corse de dos digitos
                // de :GV#. La app la muestra tal cual.
                _out->print(FW_VERSION);
                _out->print("#");
            }
            else if (codigo == "DM") {
                // El modo lo fija el jumper al arrancar, y enfocador y rotador
                // son productos distintos con su propio driver ASCOM. Sin esto,
                // conectar el driver equivocado funcionaría mal en silencio en
                // vez de avisar.
                _out->print(_isRotator ? "ROTATOR#" : "FOCUSER#");
            }
            else if (codigo == "DH") {
                // Para que se compilo este firmware: motor y chip, como
                // "35BY48;5". El driver lo usa para elegir el .bin de
                // actualizacion, que se llama astrok_<motor>_v..., y para no
                // grabar la imagen de otro chip (el numero es el chip_id de la
                // cabecera de las imagenes ESP32: 5 = C3, 9 = S3).
                _out->print(FW_MOTOR);
                _out->print(";");
                _out->print(CONFIG_IDF_FIRMWARE_CHIP_ID);
                _out->print("#");
            }
            break;

        case 'B': // BLUETOOTH: PIN de acceso (ver BleLink.h)
            if (codigo == "BQ") {
                // ¿Tiene ya PIN? Así la app sabe si pedirlo o pedir uno nuevo.
                _out->print(bleLink.pinDefinido() ? "1#" : "0#");
            }
            else if (codigo == "BA") {
                // Por USB no hace falta: se contesta OK sin mirar.
                if (!porBle || bleLink.autenticar(params)) _out->print("OK#");
                else if (!BleLink::pinValido(params)) _out->print("ER:PARAMS#");
                else _out->print("ER:PIN#");
            }
            else if (porBle) {
                // Ver, cambiar o borrar el PIN solo por USB: quien tiene el
                // cable tiene el equipo en la mano.
                _out->print("ER#");
            }
            else if (codigo == "BP") {
                if (params.length() == 0) {
                    _out->print(bleLink.pinDefinido() ? bleLink.pin() : String("-"));
                    _out->print("#");
                } else {
                    _out->print(bleLink.fijarPin(params) ? "OK#" : "ER:PARAMS#");
                }
            }
            else if (codigo == "BR") {
                bleLink.borrarPin();
                _out->print("OK#");
            }
            break;

        case 'U': // ACTUALIZACION DE FIRMWARE por USB (ver atenderActualizacion)
            atenderActualizacion(codigo, params);
            break;

        case 'W': // WIFI
            if (codigo == "WI") {
                // Estado de red: modo e IP actual, para poder verla desde el
                // driver sin depender de tener acceso por red.
                wifi_mode_t modo = WiFi.getMode();
                if (modo == WIFI_STA && WiFi.status() == WL_CONNECTED) {
                    _out->print("STA:");
                    _out->print(WiFi.localIP().toString());
                } else if (modo == WIFI_AP || modo == WIFI_AP_STA) {
                    _out->print("AP:");
                    _out->print(WiFi.softAPIP().toString());
                } else {
                    _out->print("OFF:0.0.0.0");
                }
                _out->print("#");
            }
            else if (codigo == "WR") {
                // Los cambios de WiFi solo se aplican al arrancar.
                _out->print("OK#");
                _out->flush();
                delay(200);
                ESP.restart();
            }
            break;
    }
}

void FocuserProtocol::enviarRespuestaHex(long valor) {
    // Antes se mandaban siempre 4 cifras hex (16 bits), lo que cortaba el
    // recorrido del enfocador en 65535 pasos. Aqui van al menos 4 y las que
    // hagan falta por encima, y el driver lee el hex entero. Un negativo
    // sale en complemento a 2 de 32 bits (8 cifras), que es lo que da
    // int.Parse(..., HexNumber) en el driver.
    char hex[12];
    snprintf(hex, sizeof(hex), "%04lX", (unsigned long)valor);
    _out->print(hex);
    _out->print("#");
}
// --- Actualizacion de firmware por USB ---------------------------------
// La imagen va a la particion OTA que no esta en uso, asi que el firmware que
// corre no se toca hasta el final: si el envio se corta, se cancela o llega
// corrupto, el equipo sigue arrancando con el de antes. Solo Update.end(),
// con el MD5 comprobado, cambia la particion de arranque.
//
//   :UB<tamano>,<md5>#     empieza. tamano en decimal, md5 en 32 hex.
//   :UB<tamano>,<md5>,W#   lo mismo, pero para la web: la imagen LittleFS va
//                          a la particion de datos y no a la del programa.
//   :UD<offset>,<b64>#     un trozo. offset en hex, tiene que ser el siguiente
//                          byte esperado: un trozo repetido o saltado se
//                          rechaza en vez de dejar un hueco en la imagen.
//   :UF<offset>,<n>#       n bytes a 0xFF (los dos en hex, n hasta 64 KB), sin
//                          mandarlos. La imagen LittleFS es casi toda flash
//                          vacia: mklittlefs empieza a colocar ficheros en un
//                          bloque al azar, y delante quedan cientos de KB a FF.
//                          Desde 1.4.2; el driver solo lo usa para la web.
//   :UE#                 termina: verifica el MD5, marca la particion nueva
//                          para el siguiente arranque y reinicia.
//   :UA#                   cancela.
//   :US#                   cuantos bytes lleva escritos, en hex.
//
// Cada comando responde "OK#" o "ER:<motivo>#", y el driver no manda el
// siguiente trozo hasta recibir el OK: con eso el buffer de recepcion serie
// (2048) nunca se desborda aunque escribir en flash pare el bucle un rato.
//
// :US# es la red de seguridad: si un trozo se queda sin contestar, el driver
// pregunta cuanto se ha escrito de verdad y sigue desde ahi, sin duplicar ni
// saltar nada, se haya perdido la peticion o solo la respuesta.
//
// La web no tiene la misma red: solo hay una particion y se escribe encima.
// Si se corta a medias, al arrancar LittleFS.begin(true) no la reconoce y la
// formatea, y la pagina de configuracion sale vacia hasta repetir la
// actualizacion; el programa y la configuracion (en NVS) no se tocan. La
// imagen llega recortada a lo que ocupa: lo que quede detras de la vieja son
// bloques que ningun directorio referencia, y LittleFS los da por libres.
// Un firmware anterior a esto contesta ER:PARAMS a la ",W": el md5 le sale
// de 34 caracteres.
static size_t otaTamano = 0;
static size_t otaRecibido = 0;
static unsigned long otaUltimoMs = 0;
static bool otaWeb = false;

// El driver reintenta un trozo durante unos segundos y espera 15 s al :UE#;
// pasado esto sin noticias ya no va a volver.
#define OTA_ABANDONO_MS 30000

bool FocuserProtocol::actualizandoWeb() const { return otaWeb; }

int FocuserProtocol::progresoActualizacion() {
    if (!Update.isRunning() || otaTamano == 0) return -1;
    if (millis() - otaUltimoMs > OTA_ABANDONO_MS) {
        Update.abort();
        otaTamano = 0;
        otaRecibido = 0;
        return -1;
    }
    return (int)(100ULL * otaRecibido / otaTamano);
}

// El flush no es opcional: el USB nativo del C3 (HWCDC) se queda con una
// respuesta corta en su buffer de salida hasta que se escribe algo mas detras.
// Medido: de vez en cuando un "OK#" no salia hasta la respuesta siguiente, y
// como el driver espera ese OK para mandar el trozo siguiente, la
// transferencia se quedaba parada a medias.
static void otaResponder(Print& out, const char* error) {
    if (error) { out.print("ER:"); out.print(error); out.print("#"); }
    else out.print("OK#");
    out.flush();
}

void FocuserProtocol::atenderActualizacion(const String& codigo, const String& params) {
    if (codigo == "UB") {
        if (Update.isRunning()) Update.abort();
        otaTamano = 0;
        otaRecibido = 0;

        // Escribir en flash con el motor en marcha da pasos a tirones, y el
        // reinicio del final lo pararia de golpe.
        if (_stepper.distanceToGo() != 0) { otaResponder(*_out, "MOVING"); return; }

        int coma = params.indexOf(',');
        long tamano = (coma > 0) ? params.substring(0, coma).toInt() : 0;
        String md5 = (coma > 0) ? params.substring(coma + 1) : "";
        bool web = md5.endsWith(",W");
        if (web) md5 = md5.substring(0, md5.length() - 2);
        if (tamano <= 0 || md5.length() != 32) { otaResponder(*_out, "PARAMS"); return; }

        // Con la web montada, el servidor podria leer ficheros a medio
        // escribir; tras :UE# se reinicia y se vuelve a montar.
        if (web) LittleFS.end();
        otaWeb = web;
        if (!Update.begin((size_t)tamano, web ? U_SPIFFS : U_FLASH)) { otaResponder(*_out, Update.errorString()); return; }
        if (!Update.setMD5(md5.c_str())) { Update.abort(); otaResponder(*_out, "MD5"); return; }
        otaTamano = (size_t)tamano;
        otaUltimoMs = millis();
        otaResponder(*_out, nullptr);
    }
    else if (codigo == "UD") {
        if (!Update.isRunning()) { otaResponder(*_out, "NOT_STARTED"); return; }

        int coma = params.indexOf(',');
        if (coma <= 0) { otaResponder(*_out, "PARAMS"); return; }
        size_t offset = (size_t)strtoul(params.substring(0, coma).c_str(), NULL, 16);
        if (offset != otaRecibido) { otaResponder(*_out, "OFFSET"); return; }

        // 1024 bytes por trozo como mucho (1368 caracteres de base64); se
        // deja margen por si el driver cambia de tamano.
        static uint8_t datos[2048];
        size_t n = 0;
        const char* b64 = params.c_str() + coma + 1;
        if (mbedtls_base64_decode(datos, sizeof(datos), &n,
                                  (const unsigned char*)b64, strlen(b64)) != 0 || n == 0) {
            otaResponder(*_out, "BASE64");
            return;
        }

        // La cabecera de la imagen lleva el chip para el que se compilo. Una
        // imagen de otro chip se graba sin quejarse, pero el bootloader la
        // rechaza al arrancar y el equipo se queda en un bucle de reinicios,
        // asi que se para aqui, antes de escribir nada.
        if (offset == 0 && !otaWeb) {
            uint16_t chip = (n >= 14) ? (uint16_t)(datos[12] | (datos[13] << 8)) : 0xFFFF;
            if (datos[0] != 0xE9 || chip != CONFIG_IDF_FIRMWARE_CHIP_ID) {
                Update.abort();
                otaResponder(*_out, "CHIP");
                return;
            }
        }

        if (otaRecibido + n > otaTamano || Update.write(datos, n) != n) {
            Update.abort();
            otaResponder(*_out, "WRITE");
            return;
        }
        otaRecibido += n;
        otaUltimoMs = millis();
        otaResponder(*_out, nullptr);
    }
    else if (codigo == "UF") {
        if (!Update.isRunning()) { otaResponder(*_out, "NOT_STARTED"); return; }

        int coma = params.indexOf(',');
        if (coma <= 0) { otaResponder(*_out, "PARAMS"); return; }
        size_t offset = (size_t)strtoul(params.substring(0, coma).c_str(), NULL, 16);
        size_t n = (size_t)strtoul(params.substring(coma + 1).c_str(), NULL, 16);
        if (offset != otaRecibido) { otaResponder(*_out, "OFFSET"); return; }
        // A 0 se podria quedar el driver repitiendolo sin avanzar.
        if (n == 0 || n > 0x10000) { otaResponder(*_out, "PARAMS"); return; }
        // Con offset 0 seria el principio de la imagen del programa, y la
        // comprobacion del chip de :UD# se saltaria.
        if (offset == 0 && !otaWeb) { otaResponder(*_out, "CHIP"); return; }
        if (otaRecibido + n > otaTamano) { Update.abort(); otaResponder(*_out, "WRITE"); return; }

        static uint8_t vacio[1024];
        static bool vacioListo = false;
        if (!vacioListo) { memset(vacio, 0xFF, sizeof(vacio)); vacioListo = true; }
        for (size_t hecho = 0; hecho < n; ) {
            size_t t = (n - hecho < sizeof(vacio)) ? n - hecho : sizeof(vacio);
            if (Update.write(vacio, t) != t) { Update.abort(); otaResponder(*_out, "WRITE"); return; }
            hecho += t;
        }
        otaRecibido += n;
        otaUltimoMs = millis();
        otaResponder(*_out, nullptr);
    }
    else if (codigo == "UE") {
        if (!Update.isRunning()) { otaResponder(*_out, "NOT_STARTED"); return; }
        if (otaRecibido != otaTamano) { Update.abort(); otaResponder(*_out, "SIZE"); return; }
        if (!Update.end()) { otaResponder(*_out, Update.errorString()); return; }
        otaResponder(*_out, nullptr);
        _out->flush();
        delay(300);
        ESP.restart();
    }
    else if (codigo == "US") {
        if (!Update.isRunning()) { otaResponder(*_out, "NOT_STARTED"); return; }
        _out->print(String((unsigned long)otaRecibido, HEX));
        _out->print("#");
        _out->flush();
    }
    else if (codigo == "UA") {
        if (Update.isRunning()) Update.abort();
        otaTamano = 0;
        otaRecibido = 0;
        otaResponder(*_out, nullptr);
    }
}
