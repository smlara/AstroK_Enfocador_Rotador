#include "ExternalDisplay.h"
#include "Config.h"

ExternalDisplay::ExternalDisplay()
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET), _enabled(false) {}

void ExternalDisplay::begin()
{
    // Intentamos inicializar. Si no responde en la dirección 0x3C, begin() devuelve false.
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
        // OLED no encontrado. Continuando sin pantalla...
        _enabled = false;
        return;
    }

    _enabled = true;
    display.setRotation(OLED_ROTATION); // Pantalla montada al reves
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.display();
}

void ExternalDisplay::dibujarCabecera()
{
    // Franja superior (0-7px): versión de firmware normalmente, o la IP
    // cuando hay conexión WiFi en modo STA (en AP se sigue viendo la
    // versión, ya que el nombre/IP del AP se muestran aparte al arrancar).
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
        display.print(F("IP: "));
        display.print(WiFi.localIP().toString());
    } else {
        if (isRotator) {
            display.print(F("AstroK ROTATOR v"));
        } else {
            display.print(F("AstroK FOCUSER v"));
        }
#ifdef FW_VERSION
        display.print(F(FW_VERSION));
#endif
    }
}

void ExternalDisplay::actualizar(long posActual, long posTarget, int multiplicador, bool moviendo)
{
    if (!_enabled)
        return;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    dibujarCabecera();

    // Valor principal (franja central). Se deja un margen de 2px respecto a
    // la cabecera (ZONA_INFERIOR_Y en vez de justo a continuación) para que
    // no queden pegadas visualmente.
    display.setCursor(0, ZONA_INFERIOR_Y);
    if (isRotator) {
        // Restamos el offset central antes de calcular los grados
        float grados = (float)(posActual - POSICION_CENTRAL) / PASOS_POR_GRADO;

        // Normalizar 0-360
        while (grados >= 360.0)
            grados -= 360.0;
        while (grados < 0.0)
            grados += 360.0;

        // Evitar mostrar 360.0 por redondeo al imprimir con 1 decimal
        if (grados >= 359.95) grados = 0.0;

        display.setTextSize(2);
        display.print(grados, 1);
    } else {
        display.setTextSize(2);
        display.print(posActual);
    }

    // Multiplicador: mismo renglón que el valor principal, pegado al borde
    // derecho (no a continuación del número, para no solaparse con él).
    String multTxt;
    switch (multiplicador) { // multiplicador aquí recibe el estado casteado a int
        case 0: multTxt = F("x0.1 deg"); break;   // MULT_0_1
        case 1: multTxt = F("x1 deg"); break;     // MULT_1
        case 2: multTxt = F("x10 steps"); break;  // MULT_10_STEP
        case 3: multTxt = F("x100 steps"); break; // MULT_100_STEP
        case 4: multTxt = F("x500 steps"); break;  // MULT_500_STEP
        default: multTxt = F("ERR"); break;
    }
    // Con 6 cifras (el enfocador llega a 999999) el numero ocupa 72 px y
    // "x500 steps" otros 60: no caben juntos en 128. Se quita la unidad.
    if (!isRotator && labs(posActual) >= 100000) multTxt.replace(" steps", "");
    display.setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(multTxt, 0, 0, &bx, &by, &bw, &bh);
    // Centrado verticalmente respecto a la altura del valor grande (tamaño 2,
    // 16px), no pegado a su borde superior.
    int multY = ZONA_INFERIOR_Y + (16 - 8) / 2;
    display.setCursor(SCREEN_WIDTH - bw, multY);
    display.print(multTxt);

    display.display();
}

void ExternalDisplay::mostrarMensaje(String msg, uint8_t size)
{
    if (!_enabled)
        return;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    dibujarCabecera();

    // El mensaje se centra solo en la franja inferior (debajo de la
    // cabecera), que sigue visible con la versión/IP mientras dura el mensaje.
    display.setTextSize(size);
    int zonaInferior = SCREEN_HEIGHT - ZONA_INFERIOR_Y;
    int y = ZONA_INFERIOR_Y + (zonaInferior - (8 * size)) / 2;
    if (y < ZONA_INFERIOR_Y) y = ZONA_INFERIOR_Y;
    display.setCursor(0, y);
    display.println(msg);
    display.display();
}

void ExternalDisplay::mostrarProgreso(int porcentaje, bool web)
{
    if (!_enabled)
        return;
    if (porcentaje < 0) porcentaje = 0;
    if (porcentaje > 100) porcentaje = 100;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(web ? F("UPDATING WEB...") : F("UPDATING..."));

    String pct = String(porcentaje) + "%";
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(pct, 0, 0, &bx, &by, &bw, &bh);
    display.setCursor(SCREEN_WIDTH - bw, 0);
    display.print(pct);

    // Barra en la franja inferior: marco y relleno proporcional.
    const int alto = SCREEN_HEIGHT - ZONA_INFERIOR_Y;
    display.drawRect(0, ZONA_INFERIOR_Y, SCREEN_WIDTH, alto, SSD1306_WHITE);
    int relleno = (SCREEN_WIDTH - 4) * porcentaje / 100;
    if (relleno > 0)
        display.fillRect(2, ZONA_INFERIOR_Y + 2, relleno, alto - 4, SSD1306_WHITE);
    display.display();
}

void ExternalDisplay::mostrarConexionWiFi(String status, String info) {
    if (!_enabled) return;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // Sin cabecera ni línea separadora: en el OLED de 0.91" (32px) solo caben
    // dos líneas de texto, así que usamos la primera y la última.
    display.setCursor(0, 0);
    display.println(status);

    display.setCursor(0, SCREEN_HEIGHT - 8);
    display.println(info);

    display.display();
}