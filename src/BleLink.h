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
 * Seguridad: RX exige un enlace cifrado y autenticado, así que solo un equipo
 * emparejado puede mandar comandos. El emparejamiento es por clave: el equipo
 * genera un PIN de 6 cifras al azar, lo enseña en la OLED (pinPendiente()) y
 * el PC lo teclea. El vínculo queda guardado en NVS por los dos lados.
 *
 * El anuncio lleva datos de fabricante 0xFFFF 'A' 'K' <'F'|'R'>: el modo del
 * equipo, para que la app filtre enfocadores y rotadores sin conectar (antes
 * de emparejar no podría preguntar :DM#).
 */
class BleLink : public Stream {
public:
    // nombre: el que se anuncia, p. ej. "AstroK_7BACC0".
    void begin(const String& nombre, bool rotador);
    bool activo() const;
    bool conectado() const;
    // PIN que el PC tiene que teclear para emparejarse, o -1 si no hay un
    // emparejamiento en curso.
    long pinPendiente() const;
    // Borra los vínculos guardados: el PC tendrá que volver a emparejarse.
    void olvidarEmparejamientos();

    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t n) override;
    void flush() override;
};

extern BleLink bleLink;

#endif
