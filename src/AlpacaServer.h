#ifndef ALPACASERVER_H
#define ALPACASERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>
#include <LittleFS.h>

/*
 * === ASCOM ALPACA ENDPOINT DOCUMENTATION ===
 *
 * === UDP DISCOVERY ===
 * PORT: 32227
 * - Listens for "alpacadiscovery1" and replies with {"alpacaport": 11111}
 *
 * === MANAGEMENT API ===
 * GET /management/apiversions                 -> Returns supported API versions (1)
 * GET /management/v1/description              -> Returns global server description
 * GET /management/v1/configureddevices        -> Returns configured Focuser/Rotator devices
 *
 * === COMMON DEVICE API (Focuser: /api/v1/focuser/0/ | Rotator: /api/v1/rotator/0/) ===
 * PUT /.../action                             -> (Not Implemented) Custom actions
 * PUT /.../commandblind                       -> (Not Implemented) Blind commands
 * PUT /.../commandbool                        -> (Not Implemented) Bool commands
 * PUT /.../commandstring                      -> (Not Implemented) String commands
 * GET/PUT /.../connected                      -> Get/Set connected state (tracked)
 * GET /.../description                        -> Device specific description
 * GET /.../driverinfo                         -> Driver information
 * GET /.../driverversion                      -> Driver version string
 * GET /.../interfaceversion                   -> Interface version (3)
 * GET /.../name                               -> Device name
 * GET /.../supportedactions                   -> List of custom actions (Empty)
 *
 * === FOCUSER SPECIFIC API (/api/v1/focuser/0/) ===
 * GET /.../absolute                           -> Always true (Stepper is absolute)
 * GET /.../ismoving                           -> True if motor is currently moving
 * GET /.../maxincrement                       -> Maximum steps allowed per move
 * GET /.../maxstep                            -> Maximum physical steps of the focuser
 * GET /.../position                           -> Current step position
 * GET /.../stepsize                           -> Size of one step (Microns/Arbitrary)
 * GET/PUT /.../tempcomp                       -> Get/Set temperature compensation (False)
 * GET /.../tempcompavailable                  -> True if temp comp is available (False)
 * GET /.../temperature                        -> NotImplemented (sin sensor)
 * PUT /.../halt                               -> Stop motor immediately
 * PUT /.../move (Position=X)                  -> Move to absolute step position
 *
 * === ROTATOR SPECIFIC API (/api/v1/rotator/0/) ===
 * GET /.../canreverse                         -> True if rotator supports reversing
 * GET /.../ismoving                           -> True if motor is currently moving
 * GET /.../mechanicalposition                 -> Current mechanical position in degrees
 * GET /.../position                           -> Current logical position in degrees (0-360)
 * GET /.../targetposition                     -> Target position in degrees (0-360)
 * GET /.../stepsize                           -> Degrees per step
 * PUT /.../halt                               -> Stop motor immediately
 * PUT /.../move (Position=X)                  -> Move relative X degrees (Wrapped 360)
 * PUT /.../moveabsolute (Position=X)          -> Move absolute to X degrees (Shortest path, Wrapped 360)
 * PUT /.../movemechanical (Position=X)        -> Move to absolute mechanical position
 * GET/PUT /.../reverse                        -> Get/Set reverse rotation
 * PUT /.../sync (Position=X)                  -> Synchronize logical position to X degrees
 *
 * === WEB SETUP API ===
 * GET /setup/v1/focuser/0/setup               -> Serves the HTML setup page from LittleFS
 * GET /setup/v1/rotator/0/setup               -> Serves the HTML setup page from LittleFS
 * GET /api/setup/config                       -> Returns current dynamic settings as JSON
 * POST /api/setup/config                      -> Saves new settings to ESP32 Preferences
 * POST /api/setup/reset                       -> Resets current stepper position to 0
 */

#include "ExternalDisplay.h"

class AlpacaServer {
public:
    AlpacaServer(AccelStepper& stepperRef, long& targetPosRef, bool& isRotatorRef, long& maxStepsGlobalRef, bool& rotatorReverseRef, ExternalDisplay& oledRef);
    
    void begin(bool forceAP = false);
    void loop();

    // false si la configuracion tiene el WiFi apagado (wifi.enabled) y no se
    // forzo el AP: begin() no ha encendido la radio ni los servidores.
    bool wifiActivo() const { return _wifiActivo; }

    // Callback que se invoca durante las esperas bloqueantes del arranque
    // (intentos de conexión WiFi, levantado del AP). Sirve para que el
    // protocolo serie siga respondiendo mientras tanto: abrir el puerto USB
    // resetea el ESP32, así que un cliente que conecte y pregunte enseguida
    // se comería todo ese silencio y daría timeout.
    void setTickCallback(void (*cb)());

    // Callback que se invoca tras guardar la configuración por la web, para
    // releerla y aplicarla al motor sin reiniciar: es el mismo camino que ya
    // usaba el comando serie :CS#. Sin esto, NVS y la RAM se quedan contando
    // cosas distintas hasta el siguiente arranque.
    void setConfigAppliedCallback(void (*cb)());

private:
    bool _wifiActivo = false;
    AccelStepper& stepper;
    long& targetPos;
    bool& isRotator;
    long& maxStepsGlobal;
    bool& rotatorReverse;
    ExternalDisplay& oled;

    WebServer* server;
    WebServer* server80;
    WiFiUDP udp;

    bool isConnected;
    void (*tickCallback)();
    void (*configAppliedCallback)();

    unsigned long lastTransactionID;

    // delay() troceado que va llamando a tickCallback mientras espera
    void tickDelay(unsigned long ms);

    void setupDiscovery();
    void handleDiscovery();

    void setupManagementAPI();
    void setupFocuserAPI();
    void setupRotatorAPI();
    void setupWebConfigAPI();

    void sendJsonResponse(JsonDocument& doc, uint32_t clientID, uint32_t clientTransID, int errorNum = 0, const char* errorMsg = "");

    // Helper para argumentos case-insensitive
    String getArgCaseInsensitive(const char* name);
    uint32_t getClientID();
    uint32_t getClientTransactionID();

    // Generic Handlers
    void handleNotFound();
    bool handleFileRead(String path);
    
    // Web Setup Handlers
    void handleGetConfig();
    void handleSetConfig();
    void handleFactoryReset();
    void handleResetPosition();
    void handleStatus();
    void handleWifiScan();

    // Management Handlers
    void handleApiVersions();
    void handleDescription();
    void handleConfiguredDevices();

    // Device Handlers
    void handleAction();
    void handleCommandBlind();
    void handleCommandBool();
    void handleCommandString();
    void handleConnected();
    void handleDescriptionDevice();
    void handleDriverInfo();
    void handleDriverVersion();
    void handleInterfaceVersion();
    void handleName();
    void handleSupportedActions();

    // Focuser Handlers
    void handleFocuserAbsolute();
    void handleFocuserIsMoving();
    void handleFocuserMaxIncrement();
    void handleFocuserMaxStep();
    void handleFocuserPosition();
    void handleFocuserStepSize();
    void handleFocuserTempComp();
    void handleFocuserTempCompAvailable();
    void handleFocuserTemperature();
    void handleFocuserHalt();
    void handleFocuserMove();
    void handleFocuserSync();

    // Rotator Handlers
    void handleRotatorIsMoving();
    void handleRotatorPosition();
    void handleRotatorTargetPosition();
    void handleRotatorStepSize();
    void handleRotatorCanReverse();
    void handleRotatorMechanicalPosition();
    void handleRotatorHalt();
    void handleRotatorMove();
    void handleRotatorMoveAbsolute();
    void handleRotatorMoveMechanical();
    void handleRotatorSync();
    void handleRotatorReverse();
    void handleFirmwareVersion();
    
    float getRotatorDegrees(long position);
};

#endif
