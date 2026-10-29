#include "TMC2226Manager.h"

TMC2226Manager::TMC2226Manager() 
    : driver(&TMC_SERIAL_PORT, TMC_R_SENSE, TMC_ADDRESS),
      current_mA_val(0), holdCurrent_mA_val(0)
{
}

void TMC2226Manager::begin(uint16_t current_ma, uint16_t microsteps,
                           uint16_t hold_current_ma) {
    // Inicializamos el puerto Serial1 mapeando los pines RX y TX
    TMC_SERIAL_PORT.begin(115200, SERIAL_8N1, TMC_UART_RX, TMC_UART_TX);
    
    driver.begin();                 // Iniciar comunicación UART
    driver.toff(5);                 // Habilitar driver por software

    // Por defecto (GCONF.i_scale_analog = 1) el driver escala IRUN e IHOLD con
    // la tensión del pin VREF: 2.5V es el 100% y 0.5V el 20%. Es decir, el
    // potenciómetro de la placa manda sobre la corriente y los mA de la
    // configuración mienten en esa proporción: con el VREF de fábrica de BTT
    // (~1.2V) pedir 800 mA entregaba menos de la mitad.
    // Pasándolo a 0 la referencia es interna (derivada de 5VOUT) y la corriente
    // queda definida solo por UART, así que el potenciómetro deja de influir.
    driver.I_scale_analog(false);

    current_mA_val = current_ma;
    holdCurrent_mA_val = hold_current_ma;
    aplicarCorrientes();            // Ajustar corriente RMS de marcha y reposo
    setMicrosteps(microsteps);      // Configurar micropasos
    
    // StealthChop es el modo super silencioso de TMC.
    driver.en_spreadCycle(false);   // false = stealthChop habilitado
    driver.pwm_autoscale(true);     // Requerido para StealthChop
}

void TMC2226Manager::setMicrosteps(uint16_t microsteps) {
    // TMCStepper pide 0 para paso entero, no 1: con 1 no entra en ningun case,
    // no toca el registro y el driver se queda en lo que tuviera (256 de
    // arranque), asi que el motor avanza 1/256 de lo que el firmware cree.
    // Aqui se habla en micropasos por paso, donde paso entero es 1.
    driver.microsteps(microsteps == 1 ? 0 : microsteps);
}

void TMC2226Manager::setCurrent(uint16_t mA) {
    current_mA_val = mA;
    aplicarCorrientes();
}

void TMC2226Manager::setHoldCurrent(uint16_t mA) {
    holdCurrent_mA_val = mA;
    aplicarCorrientes();
}

// La librería expresa la corriente de reposo como una fracción de la de
// marcha, porque en el chip IHOLD es una escala de 0 a 31 sobre la misma
// referencia que IRUN. Aquí se pide en mA, que es lo que el usuario sabe
// comparar con la de marcha, y la conversión se hace en un único sitio.
//
// La resolución es la del driver: 32 escalones sobre la corriente de marcha,
// así que la real cae en el escalón representable más próximo y puede quedar
// por encima de lo pedido (800/200 da IHOLD=6, que son ~214 mA). El escalón
// aplicado se lee en :DS#, en IHOLD.
void TMC2226Manager::aplicarCorrientes() {
    if (current_mA_val == 0) return;

    uint16_t hold = holdCurrent_mA_val;
    if (hold > current_mA_val) hold = current_mA_val; // en reposo nunca más que en marcha

    if (hold == 0) {
        // Sin par en reposo: el eje queda suelto. Es lo que antes hacía
        // desmarcar la casilla de hold.
        driver.rms_current(current_mA_val, 0.0f);
        driver.ihold(0);
        return;
    }

    driver.iholddelay(10); // bajada suave a la corriente de reposo
    driver.rms_current(current_mA_val, (float)hold / (float)current_mA_val);
}

void TMC2226Manager::setStealthChop(bool enable) {
    driver.en_spreadCycle(!enable);
}

void TMC2226Manager::setStallThreshold(uint8_t threshold) {
    // TCOOLTHRS es el umbral de velocidad a partir del cual el driver activa
    // StallGuard: solo mide cuando TSTEP < TCOOLTHRS, es decir, por encima de
    // esa velocidad. Antes estaba a 0xFFFFF, lo que lo dejaba activo también
    // al arrancar desde parado, donde la carga leída es ruido: el resultado
    // era una colisión falsa en cada movimiento.
    // TSTEP son ciclos de reloj entre micropasos, de ahí la división.
    long defecto = isRotator ? DEFAULT_ROTATOR_STALL_MIN_SPEED : DEFAULT_FOCUSER_STALL_MIN_SPEED;
    long minSpeed = stallMinSpeedGlobal > 0 ? stallMinSpeedGlobal : defecto;
    uint32_t tcoolthrs = TMC_INTERNAL_CLOCK / (uint32_t)minSpeed;
    if (tcoolthrs > 0xFFFFF) tcoolthrs = 0xFFFFF;

    driver.TCOOLTHRS(tcoolthrs);
    driver.SGTHRS(threshold);  // Sensibilidad (0 a 255)
}

String TMC2226Manager::getDiagnostics() {
    // Una lectura fallida por UART devuelve 0 en todos los bits, que se
    // interpretaría como "todo correcto". Por eso se comprueba CRCerror.
    uint32_t drvStatus = driver.DRV_STATUS();
    bool lecturaMala = driver.CRCerror;

    String s;
    s += "SG=";      s += String(driver.SG_RESULT());
    s += " CS=";     s += String(driver.cs_actual());
    s += " STST=";   s += String(driver.stst() ? 1 : 0);   // parado
    s += " OT=";     s += String(driver.ot() ? 1 : 0);     // sobretemperatura
    s += " OTPW=";   s += String(driver.otpw() ? 1 : 0);   // aviso de temperatura
    s += " OLA=";    s += String(driver.ola() ? 1 : 0);    // bobina A abierta
    s += " OLB=";    s += String(driver.olb() ? 1 : 0);    // bobina B abierta
    s += " S2G=";    s += String((driver.s2ga() || driver.s2gb()) ? 1 : 0); // corto a masa
    s += " UVCP=";   s += String(driver.uv_cp() ? 1 : 0);  // subtensión (sin 12V)
    s += " IRUN=";   s += String(driver.irun());        // escala digital de corriente
    s += " IHOLD=";  s += String(driver.ihold());       // escala digital en parado
    s += " VS=";     s += String(driver.vsense() ? 1 : 0); // rango de sense (alta sensibilidad)
    s += " IAN=";    s += String(driver.I_scale_analog() ? 1 : 0); // 1 = el potenciómetro VREF manda
    // Micropasos leidos del chip, no los pedidos. Solo valen si MSR=1: con
    // MSR=0 el driver ignora MRES y usa los pines MS1/MS2 (MS=ms2ms1), y el
    // motor gira una fraccion de lo que el firmware cree.
    uint16_t mres = driver.microsteps();          // la libreria da 0 en paso entero
    s += " MRES=";   s += String(mres == 0 ? 1 : mres);
    s += " MSR=";    s += String(driver.mstep_reg_select() ? 1 : 0);
    s += " MS=";     s += String(driver.ms2() ? 1 : 0); s += String(driver.ms1() ? 1 : 0);
    s += " SPREAD="; s += String(driver.en_spreadCycle() ? 1 : 0);
    s += " RAW=";    s += String(drvStatus, HEX);
    if (lecturaMala) s += " (LECTURA UART DUDOSA)";
    return s;
}

uint16_t TMC2226Manager::getStallGuardResult() {
    return driver.SG_RESULT(); // 0 a 1023 (0 significa fuerza máxima/choque inminente)
}

void TMC2226Manager::attachStallInterrupt(void (*isr)()) {
    pinMode(TMC_DIAG_PIN, INPUT_PULLDOWN); // En ESP32 pull-down es buena práctica si el driver empuja a HIGH
    // TMC2209/2226 emite un pulso HIGH en DIAG cuando detecta la colisión
    attachInterrupt(digitalPinToInterrupt(TMC_DIAG_PIN), isr, RISING);
}

bool TMC2226Manager::isStalled(uint16_t limit) {
    // Si la lectura de carga baja del límite configurado, interpretamos colisión.
    // NOTA: Para más fiabilidad, es mejor leer esto mientras el motor se mueve a velocidad constante.
    return getStallGuardResult() < limit;
}