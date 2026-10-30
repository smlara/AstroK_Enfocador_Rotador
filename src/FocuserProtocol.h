#ifndef FOCUSER_PROTOCOL_H
#define FOCUSER_PROTOCOL_H

#include <Arduino.h>
#include <AccelStepper.h>

/*
 * === PROTOCOLO SERIE ASTROK ===
 * Protocol format: :[COMMAND][PARAMS]#
 * Llega por USB y, con los mismos comandos, por BLE (ver BleLink.h). La
 * respuesta sale por el canal que trajo el comando.
 * 
 * === GETTERS (GET) ===
 * :GV#    -> Get Version (Returns 10#)
 * :GP#    -> Get Current Position (Returns 16-bit HEX steps)
 * :GN#    -> Get Target Position (Returns 16-bit HEX steps)
 * :GI#    -> Get Is Moving (Returns 01# if moving, 00# if stationary)
 * :GD#    -> Get Motor Delay (Returns 02# fixed)
 * :GH#    -> Get Half-Step mode (Returns 00# always false)
 * :GT#    -> Get Temperature (sin sensor: siempre 0000#)
 * :GC#    -> Get Temperature Coefficient (sin sensor: siempre 0000#)
 * 
 * === SETTERS (SET) ===
 * :SNXXXX# -> Set New Target Position (XXXX is 16-bit HEX steps)
 *             - Rotator: Automatically uses shortest path.
 * :SPXXXX# -> Set Current Position / Sync (XXXX is 16-bit HEX steps)
 * :SDXX#   -> Set Motor Delay (Ignored)
 * :SCXXXX# -> Set Temperature Coefficient (aceptado y descartado, no hay sensor)
 * 
 * === ACTIONS ===
 * :FQ#     -> Full Quit / Halt (Stops the motor immediately)
 * :FG#     -> Full Go (Initiates move, though firmware starts move on SN)
 * :+#      -> Temperature Compensation Activate (Ignored)
 *
 * === EXTENSIONES ASTROK ===
 * Las usa el diálogo de configuración del driver ASCOM. Existen porque la
 * configuración de red solo era accesible por HTTP, lo que no sirve cuando
 * precisamente la WiFi es lo que hay que configurar o reparar.
 *
 * :CG#        -> Get Config. Devuelve el JSON completo de NVS (el mismo de
 *                /api/setup/config) codificado en base64, terminado en '#'.
 * :CS<b64>#   -> Set Config. Recibe ese JSON en base64, lo valida y lo guarda.
 *                Responde OK# o ER#. Los cambios de WiFi piden reinicio.
 * :CR#        -> Reset de fábrica: borra la configuración de NVS y el PIN
 *                de Bluetooth, y reinicia.
 *                Responde OK# antes de reiniciar. El siguiente arranque
 *                recrea los valores por defecto.
 * :DM#        -> Modo en que arrancó el equipo: "FOCUSER#" o "ROTATOR#". Lo
 *                fija el jumper de MODE. Enfocador y rotador son productos
 *                distintos, cada uno con su driver ASCOM: esto permite que el
 *                driver rechace la conexión si le toca el equipo equivocado.
 * :DS#        -> Banderas de DRV_STATUS del driver en texto. DIAG se levanta
 *                tanto por stall como por errores (subtensión, carga abierta,
 *                sobretemperatura); sin esto no se distinguen entre sí.
 * :WI#        -> Estado de red: "STA:<ip>#", "AP:<ip>#" u "OFF:0.0.0.0#".
 * :WR#        -> Reinicia el equipo (para aplicar la configuración de WiFi).
 * :DH#        -> Para qué se compiló: "<motor>;<chip_id>#", p. ej. "35BY48;5#".
 * :BQ#        -> ¿Tiene PIN de Bluetooth? "1#" o "0#".
 * :BA<pin>#   -> Da el PIN (4 cifras) en una conexión BLE: "OK#", "ER:PIN#" o
 *                "ER:PARAMS#". Sin PIN guardado, este se queda como PIN. Hasta
 *                dar el PIN, por BLE todo lo demás responde "ER:PIN#"; tras 3
 *                fallos se corta la conexión. Por USB no se pide (OK# siempre).
 * :BP#        -> Solo USB: el PIN guardado, o "-#" si no hay.
 * :BP<pin>#   -> Solo USB: fija el PIN. "OK#" o "ER:PARAMS#".
 * :BR#        -> Solo USB: borra el PIN; la próxima conexión BLE elige uno.
 * :UB<tam>,<md5>#  :UD<offset hex>,<b64>#  :UE#  :UA#  :US#
 *             -> Actualización de firmware por USB a la partición OTA libre.
 *                Ver atenderActualizacion() en FocuserProtocol.cpp.
 *
 * El JSON viaja en base64 a propósito: SSID y contraseñas pueden contener ':'
 * o '#', que son los delimitadores del protocolo, y romperían el marcado.
 * 
 * Note: In Rotator mode, all step positions are automatically wrapped and
 * normalized to the 360-degree range. Reversal (Reverse) flag is also respected.
 */

class FocuserProtocol {
public:
    // Pasamos referencias para que el protocolo pueda mover el motor directamente
    FocuserProtocol(AccelStepper& stepper, long& targetPos, bool& isRotatorRef, bool& rotatorReverseRef);
    
    void procesarSerial();
    // Los mismos comandos por BLE (ver BleLink.h).
    void procesarBle();

    // Se invoca tras guardar la configuración con :CS#. Permite que los
    // parámetros del motor (corriente, micropasos, sensibilidad del stall) se
    // apliquen al vuelo: ajustar la sensibilidad a base de reiniciar entre
    // prueba y prueba no es practicable.
    void setConfigAppliedCallback(void (*cb)());

    // Porcentaje enviado de una actualizacion por USB en curso (0-100), o -1
    // si no hay ninguna. La pantalla lo usa para enseñar la barra. Si el
    // driver deja de mandar trozos (cable fuera, app cerrada) la cancela.
    int progresoActualizacion();
    // La actualizacion en curso es de la web (LittleFS), no del programa.
    bool actualizandoWeb() const;

private:
    AccelStepper& _stepper;
    long& _targetPos;
    bool& _isRotator;
    bool& _rotatorReverse;
    String _buffer;
    String _bufferBle;
    void (*_onConfigApplied)();
    // Canal por el que llegó el comando en curso: ahí va la respuesta.
    Print* _out;

    void procesarCanal(Stream& canal, String& buffer);
    void ejecutarComando(String cmd);
    void enviarRespuestaHex(long valor);
    void atenderActualizacion(const String& codigo, const String& params);
};

#endif