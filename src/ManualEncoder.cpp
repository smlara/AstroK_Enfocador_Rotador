#include "ManualEncoder.h"
#include "Config.h" // Necesitamos saber PASOS_POR_GRADO y isRotator

ManualEncoder::ManualEncoder(uint8_t clkPin, uint8_t dtPin, uint8_t swPin)
    : _clk(clkPin), _dt(dtPin), _sw(swPin), _currentState(MULT_1), _resetRequested(false),
      _stopRequested(false), _lastClkState(HIGH), _restoPasos(0.0f),
      _botonCrudo(0), _botonEstable(0), _botonCambio(0), _botonRepetir(0),
      _buttonPressStartTime(0), _isPressed(false), _actionTaken(false),
      _lastButtonChangeTime(0)
{
}

void ManualEncoder::begin()
{
  pinMode(_clk, INPUT_PULLUP);
  pinMode(_dt, INPUT_PULLUP);
  pinMode(_sw, INPUT_PULLUP);

  if (isRotator) {
      _currentState = MULT_1;
  } else {
      _currentState = MULT_10_STEP;
  }

  _lastClkState = digitalRead(_clk);
}

bool ManualEncoder::checkResetRequest() {
    if (_resetRequested) {
        _resetRequested = false;
        return true;
    }
    return false;
}

bool ManualEncoder::checkStopRequest() {
    if (_stopRequested) {
        _stopRequested = false;
        return true;
    }
    return false;
}

long ManualEncoder::getUpdatedPosition(long currentTarget, bool isMoving)
{
  // --- 1. Lógica del Botón por Polling ---
  bool currentButtonState = digitalRead(_sw);
  
  if (currentButtonState == LOW && !_isPressed) {
      // Flanco de bajada (empieza pulsación)
      if (millis() - _lastButtonChangeTime > 50) { // Antirrebote inicial
          _isPressed = true;
          _buttonPressStartTime = millis();
          _actionTaken = false;
      }
  }
  else if (currentButtonState == LOW && _isPressed) {
      // Mantenido pulsado: chequear si llevamos > 3 segundos
      if (!_actionTaken && (millis() - _buttonPressStartTime > 3000)) {
           // Resetear posición
           _actionTaken = true;
           _resetRequested = true;
      }
  }
  else if (currentButtonState == HIGH && _isPressed) {
      // Flanco de subida (soltado)
      _isPressed = false;
      _lastButtonChangeTime = millis();
      
      // Si soltamos antes de los 3 segundos y no hemos tomado acción
      if (!_actionTaken) {
           if (millis() - _buttonPressStartTime > 50) { // Antirrebote mínimo
                if (isMoving) {
                    // Pulsación corta mientras se mueve -> parada de
                    // emergencia en vez de cambiar el multiplicador. Evita
                    // quedarte sin forma de cancelar un salto grande (p.ej.
                    // x1000) si giraste el mando más rápido de lo que el
                    // motor puede seguir.
                    _stopRequested = true;
                } else if (isRotator) {
                    if (_currentState == MULT_0_1) {
                        _currentState = MULT_1;
                    } else {
                        _currentState = MULT_0_1;
                    }
                } else {
                    if (_currentState == MULT_10_STEP) {
                        _currentState = MULT_100_STEP;
                    } else if (_currentState == MULT_100_STEP) {
                        _currentState = MULT_500_STEP;
                    } else {
                        _currentState = MULT_10_STEP;
                    }
                }
           }
      }
      _actionTaken = false;
  }

  long newTarget = currentTarget;

#if CONTROL_CON_BOTONES
  // --- 2b. Pulsadores S1/S2 en lugar del encoder ---
  // Mismo arrastre de fracción que el encoder (ver más abajo): en el Rotador
  // 0.1 grado no es un número entero de micropasos.
  int direccion = leerBotones();
  if (direccion != 0) {
      _restoPasos += direccion * pasosPorClic();
      long pasosEnteros = lroundf(_restoPasos);
      _restoPasos -= (float)pasosEnteros;
      newTarget += pasosEnteros;
  }
  return newTarget;
#endif

  // --- 2. Lógica del Encoder por Polling ---
  int clkState = digitalRead(_clk);

  // Detectar flanco de bajada (cuando giramos un paso completo, el estado suele pasar de HIGH a LOW o viceversa)
  // Generalmente con encoders mecánicos detectar solo de HIGH a LOW es suficiente para dar un "clic" seguro.
  if (clkState != _lastClkState && clkState == LOW) {
      int dtState = digitalRead(_dt);

      float pasosASumar = pasosPorClic();

      // La dirección de giro se define leyendo DT en el instante en que CLK cae.
      // Invertida respecto a la lectura eléctrica para que el sentido físico
      // de giro coincida con el esperado en este cableado.
      float delta = (dtState != clkState) ? -pasosASumar : pasosASumar;

      // Se arrastra la fracción pendiente al siguiente clic en vez de tirarla:
      // así N clics caen siempre a menos de medio micropaso del ángulo exacto,
      // en lugar de acumular el error de cada truncamiento. Con los pasos
      // enteros del enfocador el resto es siempre 0 y esto no hace nada.
      _restoPasos += delta;
      long pasosEnteros = lroundf(_restoPasos);
      _restoPasos -= (float)pasosEnteros;
      newTarget += pasosEnteros;
  }
  
  _lastClkState = clkState;

  return newTarget;
}

float ManualEncoder::pasosPorClic()
{
  switch (_currentState) {
      case MULT_0_1:      return PASOS_POR_GRADO / 10.0f; // 0.1 grado
      case MULT_1:        return PASOS_POR_GRADO;         // 1 grado
      case MULT_10_STEP:  return 10;
      case MULT_100_STEP: return 100;
      case MULT_500_STEP: return 500;
      default:            return 0;
  }
}

int ManualEncoder::leerBotones()
{
  unsigned long ahora = millis();
  bool subir = digitalRead(_clk) == LOW;
  bool bajar = digitalRead(_dt) == LOW;
  // Los dos a la vez no mueven nada: mejor quieto que adivinar cuál ganó.
  int crudo = (subir == bajar) ? 0 : (subir ? 1 : -1);

  // Antirrebote: una lectura solo cuenta cuando lleva un rato sin cambiar.
  if (crudo != _botonCrudo) {
      _botonCrudo = crudo;
      _botonCambio = ahora;
  }
  if (ahora - _botonCambio < BOTON_ANTIRREBOTE_MS) {
      return 0;
  }

  if (_botonEstable != crudo) {
      _botonEstable = crudo;
      if (crudo != 0) {
          // Recién pulsado: un paso ya, y si se mantiene, repetición.
          _botonRepetir = ahora + BOTON_REPETIR_INICIO_MS;
          return crudo;
      }
      return 0;
  }

  if (_botonEstable != 0 && (long)(ahora - _botonRepetir) >= 0) {
      _botonRepetir = ahora + BOTON_REPETIR_CADA_MS;
      return _botonEstable;
  }
  return 0;
}

EncoderState ManualEncoder::getMultiplicadorState()
{
  return _currentState;
}
