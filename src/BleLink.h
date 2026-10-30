#ifndef BLE_LINK_H
#define BLE_LINK_H

#include <Arduino.h>

/*
 * Canal BLE para el protocolo serie AstroK (los mismos comandos :XX# que por
 * USB). Usa el servicio Nordic UART: el cliente escribe en RX y las respuestas
 * le llegan como notificaciones de TX. Es un servicio que entienden las apps
 * genéricas de prueba (nRF Connect, Serial Bluetooth Terminal), y eso basta
 * para depurar sin tener aún una app propia.
 *
 *   Servicio 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX       6E400002-...  (write, write without response)
 *   TX       6E400003-...  (notify)
 *
 * Se expone como un Stream para que FocuserProtocol lo trate igual que a
 * Serial. NimBLE entrega lo recibido en su propia tarea, no en loop(): aquí
 * solo se guarda en un buffer, y el comando se ejecuta cuando loop() lo lee.
 * Así nada toca el motor ni la configuración desde otra tarea.
 *
 * Una sola conexión a la vez. Lo escrito se envía al llegar el '#' que cierra
 * cada respuesta, o con flush().
 *
 * Acceso: sin emparejamiento BLE (con Windows daba más problemas de los que
 * resolvía). En su lugar, un PIN de 4 cifras guardado en NVS: cada conexión
 * BLE tiene que darlo con :BA<pin># antes de que se le acepte ningún otro
 * comando. Si el equipo aún no tiene PIN, el primero que llega con :BA se
 * queda como PIN. Por USB no se pide nunca, y por USB se puede ver, cambiar
 * o borrar (:BP#, :BR#). El enlace no va cifrado: protege de que cualquiera
 * con un móvil mueva el motor, no de quien escuche el aire.
 *
 * El anuncio lleva datos de fabricante 0xFFFF 'A' 'K' <'F'|'R'>: el modo del
 * equipo, para que la app filtre enfocadores y rotadores sin conectar.
 */
class BleLink : public Stream {
public:
    // nombre: el que se anuncia, p. ej. "AstroK_7BACC0".
    void begin(const String& nombre, bool rotador);
    bool activo() const;
    bool conectado() const;

    // PIN de acceso. Sin PIN guardado, pin() devuelve "".
    bool pinDefinido() const;
    String pin() const;
    // Guarda un PIN nuevo (4 cifras). false si no tiene ese formato.
    bool fijarPin(const String& pin);
    void borrarPin();
    static bool pinValido(const String& pin);

    // La conexión actual ya ha dado el PIN.
    bool autenticado() const;
    // :BA<pin>#. Sin PIN guardado, este se queda como PIN. Tras varios fallos
    // seguidos corta la conexión, para que probar los 10000 no salga gratis.
    bool autenticar(const String& pin);

    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t n) override;
    void flush() override;
};

extern BleLink bleLink;

#endif
