#ifndef MANUAL_ENCODER_H
#define MANUAL_ENCODER_H

#include <Arduino.h>

#define DEBOUNCE_DELAY 200 // ms para el botón del encoder

// Estados del multiplicador
enum EncoderState {
    MULT_0_1,      // x0.1 deg (Rotador)
    MULT_1,        // x1 deg (Rotador)
    MULT_10_STEP,  // x10 pasos (Enfocador)
    MULT_100_STEP, // x100 pasos (Enfocador)
    MULT_500_STEP  // x500 pasos (Enfocador)
};

class ManualEncoder {
public:
    // Pasamos los pines al constructor
    ManualEncoder(uint8_t clkPin, uint8_t dtPin, uint8_t swPin);
    
    void begin();
    
    // Devuelve true si se solicitó resetear la posición (pulsación larga), y limpia el flag
    bool checkResetRequest();

    // Devuelve true si se solicitó parar el motor (pulsación corta mientras
    // se movía), y limpia el flag.
    bool checkStopRequest();

    // isMoving indica si el motor está en marcha en este instante: si lo
    // está, una pulsación corta detiene en vez de cambiar el multiplicador.
    long getUpdatedPosition(long currentTarget, bool isMoving);
    EncoderState getMultiplicadorState();

private:
    uint8_t _clk, _dt, _sw;
    EncoderState _currentState;
    bool _resetRequested;
    bool _stopRequested;

    // Variables para polling del encoder
    int _lastClkState;

    // Fraccion de micropaso que ha sobrado del ultimo clic. Un clic en grados
    // casi nunca cae en un numero entero de micropasos (a 8 micropasos, 0.1
    // grado son 3.775), y truncar cada clic se comeria un 20% del recorrido.
    float _restoPasos;

    // Pulsadores S1/S2 (ver CONTROL_CON_BOTONES). Se guarda la
    // dirección como +1 (S1), -1 (S2) o 0 (ninguno o los dos a la vez).
    int _botonCrudo;               // última lectura, sin filtrar
    int _botonEstable;             // lectura ya filtrada de rebotes
    unsigned long _botonCambio;    // cuándo cambió _botonCrudo por última vez
    unsigned long _botonRepetir;   // cuándo toca el siguiente paso repetido

    // Clic del encoder o del pulsador mantenido: pasos a mover según el
    // multiplicador actual (sin signo).
    float pasosPorClic();
    // Devuelve +1/-1 cuando S1/S2 generan un paso (al pulsar o al repetir), 0 si no.
    int leerBotones();

    // Variables para el botón
    unsigned long _buttonPressStartTime;
    bool _isPressed;
    bool _actionTaken;
    unsigned long _lastButtonChangeTime;
};

#endif