#ifndef EXTERNAL_DISPLAY_H
#define EXTERNAL_DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <WiFi.h>

// OLED 0.91" 128x32 (antes 0.96" 128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
// Orientacion de la pantalla: 0=normal, 2=girada 180 grados

#define OLED_ROTATION 2

// Y donde empieza la franja inferior (valor/mensajes), dejando margen
// respecto a la cabecera (que ocupa 0-7) para que no queden pegadas.
#define ZONA_INFERIOR_Y 14

// En el nano, SDA va en A4 y SCL en A5, pero la librería Wire lo maneja automáticamente, 
class ExternalDisplay
{
public:
    ExternalDisplay();
    void begin();
    void actualizar(long posActual, long posTarget, int multiplicador, bool moviendo);
    void mostrarMensaje(String msg, uint8_t size = 1);
    void mostrarConexionWiFi(String status, String info);
    // "UPDATING..." (o "UPDATING WEB...") con el porcentaje y una barra,
    // durante la actualizacion por USB.
    void mostrarProgreso(int porcentaje, bool web);

private:
    Adafruit_SSD1306 display;
    bool _enabled; // Flag para saber si la pantalla respondió al inicio

    // Franja superior (8px) compartida por actualizar() y mostrarMensaje():
    // versión de firmware, o la IP si hay una conexión WiFi en modo STA.
    void dibujarCabecera();
};

#endif