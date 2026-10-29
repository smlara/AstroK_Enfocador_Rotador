#ifndef TMC2226_MANAGER_H
#define TMC2226_MANAGER_H

#include <Arduino.h>
#include <TMCStepper.h>
#include "Config.h"

class TMC2226Manager {
public:
    TMC2226Manager();
    
    // Inicializa el puerto UART y la configuración básica del TMC2226
    void begin(uint16_t current_ma = 800, uint16_t microsteps = 32,
               uint16_t hold_current_ma = 200);
    
    // Ajusta la resolución de micropasos (ej. 8, 16, 32)
    void setMicrosteps(uint16_t microsteps);
    
    // Ajusta la corriente del motor en miliamperios
    void setCurrent(uint16_t mA);
    
    // Corriente que se mantiene con el motor parado, en mA RMS. 0 deja el eje
    // suelto (IHOLD=0) y sustituye al antiguo interruptor de hold. No puede
    // superar a la de marcha; si se pide mas, se acota.
    void setHoldCurrent(uint16_t mA);
    
    // Habilita el modo StealthChop para que sea silencioso (true = activado)
    void setStealthChop(bool enable);

    // Ajusta la sensibilidad de la detección de choque/final de carrera (0 a 255)
    // Valores más altos hacen que salte con menos fuerza.
    void setStallThreshold(uint8_t threshold);

    // Devuelve las banderas de DRV_STATUS en texto. DIAG se levanta tanto por
    // stall como por errores del driver (subtensión, carga abierta,
    // sobretemperatura), así que sin esto es imposible distinguir un choque
    // real de un problema de alimentación o de cableado del motor.
    String getDiagnostics();

    // Lee la carga actual del motor. Valor más bajo indica mayor esfuerzo.
    // Usado para la detección sin sensores (Sensorless Homing).
    uint16_t getStallGuardResult();

    // Configura el pin DIAG y la interrupción. Recibe la función ISR de callback.
    void attachStallInterrupt(void (*isr)());

    // Comprueba si la carga supera el límite (es decir, si detecta tope)
    // Recomendado ajustar `stallThreshold` y usar el pin DIAG si se necesita rapidez,
    // o leer repetidamente esta función si solo se usa UART.
    bool isStalled(uint16_t limit = 100);

private:
    // La clase TMC2209Stepper sirve también para TMC2226 ya que comparten 
    // el mismo mapa de registros por UART.
    TMC2209Stepper driver;
    // IRUN e IHOLD salen del mismo registro y la libreria los escribe juntos,
    // asi que cualquier cambio en una de las dos reaplica las dos.
    void aplicarCorrientes();

    uint16_t current_mA_val;
    uint16_t holdCurrent_mA_val;
};

#endif