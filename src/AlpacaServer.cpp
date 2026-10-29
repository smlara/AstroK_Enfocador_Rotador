#include "AlpacaServer.h"
#include "Config.h" // For POSICION_CENTRAL, PASOS_POR_GRADO, etc.
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

extern Preferences preferences;

#define ALPACA_DISCOVERY_PORT 32227
#define HTTP_PORT 11111

// Diagnóstico del arranque de WiFi por el puerto serie. Desactivado por
// defecto porque ese puerto lo comparte el protocolo serie: un cliente que
// abra el puerto (lo que resetea el ESP32 por el USB nativo) y consulte de
// inmediato se encontraría este texto en vez de una respuesta limpia.
// Para activarlo, compilar con -D DEBUG_WIFI.
#ifdef DEBUG_WIFI
  #define WIFI_LOG(...) Serial.printf(__VA_ARGS__)
#else
  #define WIFI_LOG(...) ((void)0)
#endif

AlpacaServer::AlpacaServer(AccelStepper& stepperRef, long& targetPosRef, bool& isRotatorRef, long& maxStepsGlobalRef, bool& rotatorReverseRef, ExternalDisplay& oledRef)
    : stepper(stepperRef),
      targetPos(targetPosRef),
      isRotator(isRotatorRef),
      maxStepsGlobal(maxStepsGlobalRef),
      rotatorReverse(rotatorReverseRef),
      oled(oledRef),
      server(nullptr),
      isConnected(false),
      tickCallback(nullptr),
      configAppliedCallback(nullptr),
      lastTransactionID(0) {
}

void AlpacaServer::setTickCallback(void (*cb)()) {
    tickCallback = cb;
}

void AlpacaServer::setConfigAppliedCallback(void (*cb)()) {
    configAppliedCallback = cb;
}

void AlpacaServer::tickDelay(unsigned long ms) {
    unsigned long inicio = millis();
    do {
        if (tickCallback) tickCallback();
        delay(5);
    } while (millis() - inicio < ms);
}

void AlpacaServer::begin(bool forceAP) {
    // Inicializar LittleFS para web setup
    if (!LittleFS.begin(true)) {
        // Falló LittleFS
    }
    
    String configJson = preferences.getString("config", "{}");
    JsonDocument wifiDoc;
    DeserializationError error = deserializeJson(wifiDoc, configJson);
    WIFI_LOG("[WiFi] config NVS: %s (%s) len=%u\n",
             error ? "ERROR AL PARSEAR JSON" : "OK", error.c_str(), configJson.length());

    // WiFi apagado: no se toca la radio ni se crean los servidores; el equipo
    // se maneja solo por USB. Una config sin la clave (anterior a 1.4.3) lo
    // tiene encendido. Forzar el AP con el encoder lo enciende igualmente,
    // para poder volver a la web aunque se apagara por error.
    bool habilitado = error || (wifiDoc["wifi"]["enabled"] | DEFAULT_WIFI_ENABLED);
    if (!habilitado && !forceAP) {
        WIFI_LOG("[WiFi] desactivado en la configuracion\n");
        _wifiActivo = false;
        return;
    }
    _wifiActivo = true;

    // Intentar conectar a WiFi desde config NVS
    bool wifiConnected = false;
    WiFi.mode(WIFI_STA);

    String finalApName = "AstroK_AP";
    String finalApPass = "";

    if (!error && !wifiDoc["wifi"].isNull()) {
        JsonObject wifi = wifiDoc["wifi"];
        int timeout_ms = wifi["timeout_ms"] | 1000;
        int retries = forceAP ? 0 : (wifi["retries"] | 10);
        JsonArray networks = wifi["networks"].as<JsonArray>();

        bool configChanged = false;
        
        if (wifi["ap_name"].isNull() || wifi["ap_name"].as<String>() == "") {
            String mac = WiFi.macAddress();
            mac.replace(":", "");
            wifi["ap_name"] = "AstroK_" + mac.substring(6);
            configChanged = true;
        }
        if (wifi["ap_password"].isNull()) {
            wifi["ap_password"] = "";
            configChanged = true;
        }
        
        finalApName = wifi["ap_name"].as<String>();
        finalApPass = wifi["ap_password"].as<String>();
        WIFI_LOG("[WiFi] ap_name='%s' redes guardadas=%d retries=%d timeout_ms=%d forceAP=%d\n",
                 finalApName.c_str(), networks.size(), retries, timeout_ms, forceAP);

        if (configChanged) {
            String outStr;
            serializeJson(wifiDoc, outStr);
            preferences.putString("config", outStr);
        }

        for (int r = 0; r < retries && !wifiConnected; r++) {
            for (JsonVariant net : networks) {
                const char* ssid = net["ssid"];
                const char* pass = net["password"];

                if (ssid) {
                    WIFI_LOG("[WiFi] intento %d: SSID '%s'... ", r + 1, ssid);
                    oled.mostrarConexionWiFi("Trying SSID:", String(ssid));
                    WiFi.begin(ssid, pass ? pass : "");
                    unsigned long startAttemptTime = millis();

                    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout_ms) {
                        tickDelay(100);
                    }

                    WIFI_LOG("status=%d (%s)\n", WiFi.status(),
                             WiFi.status() == WL_CONNECTED ? "CONECTADO" : "fallo/timeout");
                    if (WiFi.status() == WL_CONNECTED) {
                        wifiConnected = true;
                        break;
                    }
                }
            }
        }
    }

    if (!wifiConnected) {
        // Falló la conexión o no hay JSON, iniciar como Access Point (AP)
        WiFi.disconnect();
        WiFi.mode(WIFI_AP_STA);
        tickDelay(100); // Da tiempo al driver a completar el cambio de modo antes de softAP()

        // Si el archivo JSON no se pudo leer, generamos el MAC por defecto
        if (finalApName == "AstroK_AP") {
            String mac = WiFi.macAddress();
            mac.replace(":", "");
            finalApName = "AstroK_" + mac.substring(6);
        }

        oled.mostrarConexionWiFi("Starting AP...", finalApName);
        // IP explícita del AP: algunos cores de ESP32 emiten el beacon de
        // forma poco fiable en modo AP_STA si no se fija con softAPConfig()
        // antes de softAP().
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        bool apOk;
        if (finalApPass.length() >= 8) {
            apOk = WiFi.softAP(finalApName.c_str(), finalApPass.c_str());
        } else {
            // WPA2 requiere al menos 8 caracteres para la contraseña
            apOk = WiFi.softAP(finalApName.c_str());
        }
        (void)apOk; // Solo se consulta en el log de diagnóstico (DEBUG_WIFI)
        tickDelay(100); // Deja que el AP termine de levantarse antes de leer su estado
        WIFI_LOG("[WiFi] softAP('%s') -> %s, IP=%s, canal=%d, clientes=%d\n", finalApName.c_str(),
                 apOk ? "OK" : "FALLO", WiFi.softAPIP().toString().c_str(),
                 WiFi.channel(), WiFi.softAPgetStationNum());
    }

    // Cargar configuraciones guardadas

    server = new WebServer(HTTP_PORT);
    server80 = new WebServer(80);
    // Por defecto handleClient() hace delay(1) cuando no hay cliente, y se
    // llama en cada vuelta de loop(), que es tambien la que da los pasos
    // (AccelStepper da como mucho uno por llamada a run()). Con dos servidores
    // el bucle se quedaba en ~450 vueltas/s y el motor no pasaba de ~440
    // pasos/s, pidiera lo que pidiera max_speed.
    server->enableDelay(false);
    server80->enableDelay(false);

    setupDiscovery();
    setupManagementAPI();
    setupFocuserAPI();
    setupRotatorAPI();
    setupWebConfigAPI();

    server->onNotFound([this]() { handleNotFound(); });
    server->begin(HTTP_PORT);

    server80->onNotFound([this]() { 
        String host = server80->hostHeader();
        int colonPos = host.indexOf(':');
        if (colonPos != -1) {
            host = host.substring(0, colonPos);
        }
        if (host == "") {
            if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
                host = WiFi.softAPIP().toString();
            } else {
                host = WiFi.localIP().toString();
            }
        }
        
        String url = "http://" + host + ":" + String(HTTP_PORT) + server80->uri();
        
        // Mantener argumentos (query string)
        if (server80->args() > 0) {
            url += "?";
            for (int i = 0; i < server80->args(); i++) {
                if (i > 0) url += "&";
                url += server80->argName(i) + "=" + server80->arg(i);
            }
        }
        
        server80->sendHeader("Location", url, true);
        server80->send(301, "text/plain", "");
    });
    server80->begin(80);
}

void AlpacaServer::loop() {
    if (!_wifiActivo) return;
    handleDiscovery();
    if (server) {
        server->handleClient();
    }
    if (server80) {
        server80->handleClient();
    }
}

void AlpacaServer::setupDiscovery() {
    udp.begin(ALPACA_DISCOVERY_PORT);
}

void AlpacaServer::handleDiscovery() {
    int packetSize = udp.parsePacket();
    if (packetSize) {
        char packetBuffer[255];
        int len = udp.read(packetBuffer, 255);
        if (len > 0) {
            packetBuffer[len] = 0;
        }
        
        String request = String(packetBuffer);
        if (request.indexOf("alpacadiscovery1") >= 0) {
            udp.beginPacket(udp.remoteIP(), udp.remotePort());
            udp.print("{\"AlpacaPort\": " + String(HTTP_PORT) + "}");
            udp.endPacket();
        }
    }
}

void AlpacaServer::sendJsonResponse(JsonDocument& doc, uint32_t clientID, uint32_t clientTransID, int errorNum, const char* errorMsg) {
    doc["ClientID"] = clientID;
    doc["ClientTransactionID"] = clientTransID;
    doc["ServerTransactionID"] = ++lastTransactionID;
    doc["ErrorNumber"] = errorNum;
    doc["ErrorMessage"] = errorMsg;

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

String AlpacaServer::getArgCaseInsensitive(const char* name) {
    String lowerName = String(name);
    lowerName.toLowerCase();
    for (int i = 0; i < server->args(); i++) {
        String argName = server->argName(i);
        argName.toLowerCase();
        if (argName == lowerName) {
            return server->arg(i);
        }
    }
    return "";
}

uint32_t AlpacaServer::getClientID() {
    String val;
    if (server->method() == HTTP_GET) {
        val = getArgCaseInsensitive("ClientID");
    } else {
        val = server->arg("ClientID");
    }
    if (val.isEmpty()) return 0;
    long id = val.toInt();
    return id < 0 ? 0 : (uint32_t)id;
}

uint32_t AlpacaServer::getClientTransactionID() {
    String val;
    if (server->method() == HTTP_GET) {
        val = getArgCaseInsensitive("ClientTransactionID");
    } else {
        val = server->arg("ClientTransactionID");
    }
    if (val.isEmpty()) return 0;
    long id = val.toInt();
    return id < 0 ? 0 : (uint32_t)id;
}

void AlpacaServer::setupManagementAPI() {
    server->on("/management/apiversions", HTTP_GET, [this]() { handleApiVersions(); });
    server->on("/management/v1/description", HTTP_GET, [this]() { handleDescription(); });
    server->on("/management/v1/configureddevices", HTTP_GET, [this]() { handleConfiguredDevices(); });
    server->on("/api/v1/firmwareversion", HTTP_GET, [this]() { handleFirmwareVersion(); });
}

void AlpacaServer::handleApiVersions() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    
    JsonDocument doc;
    JsonArray value = doc["Value"].to<JsonArray>();
    value.add(1);
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleDescription() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    
    JsonDocument doc;
    JsonObject value = doc["Value"].to<JsonObject>();
    value["ServerName"] = "AstroK Alpaca Server";
    value["Manufacturer"] = "Sergio Martínez Lara";
    value["ManufacturerVersion"] = FW_VERSION;
    value["Location"] = "Madrid, Spain";
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleConfiguredDevices() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    
    JsonDocument doc;
    JsonArray value = doc["Value"].to<JsonArray>();
    
    JsonObject device = value.add<JsonObject>();
    device["DeviceName"] = isRotator ? "AstroRotator" : "AstroFocuser";
    device["DeviceType"] = isRotator ? "Rotator" : "Focuser";
    device["DeviceNumber"] = 0;
    device["UniqueID"] = isRotator ? "AstroK-Rotator-0" : "AstroK-Focuser-0";
    
    sendJsonResponse(doc, clientID, clientTransID);
}

// Common device endpoints mapping
void AlpacaServer::setupFocuserAPI() {
    server->on("/api/v1/focuser/0/action", HTTP_PUT, [this]() { handleAction(); });
    server->on("/api/v1/focuser/0/commandblind", HTTP_PUT, [this]() { handleCommandBlind(); });
    server->on("/api/v1/focuser/0/commandbool", HTTP_PUT, [this]() { handleCommandBool(); });
    server->on("/api/v1/focuser/0/commandstring", HTTP_PUT, [this]() { handleCommandString(); });
    server->on("/api/v1/focuser/0/connected", HTTP_GET, [this]() { handleConnected(); });
    server->on("/api/v1/focuser/0/connected", HTTP_PUT, [this]() { handleConnected(); });
    server->on("/api/v1/focuser/0/description", HTTP_GET, [this]() { handleDescriptionDevice(); });
    server->on("/api/v1/focuser/0/driverinfo", HTTP_GET, [this]() { handleDriverInfo(); });
    server->on("/api/v1/focuser/0/driverversion", HTTP_GET, [this]() { handleDriverVersion(); });
    server->on("/api/v1/focuser/0/interfaceversion", HTTP_GET, [this]() { handleInterfaceVersion(); });
    server->on("/api/v1/focuser/0/name", HTTP_GET, [this]() { handleName(); });
    server->on("/api/v1/focuser/0/supportedactions", HTTP_GET, [this]() { handleSupportedActions(); });

    server->on("/api/v1/focuser/0/absolute", HTTP_GET, [this]() { handleFocuserAbsolute(); });
    server->on("/api/v1/focuser/0/ismoving", HTTP_GET, [this]() { handleFocuserIsMoving(); });
    server->on("/api/v1/focuser/0/maxincrement", HTTP_GET, [this]() { handleFocuserMaxIncrement(); });
    server->on("/api/v1/focuser/0/maxstep", HTTP_GET, [this]() { handleFocuserMaxStep(); });
    server->on("/api/v1/focuser/0/position", HTTP_GET, [this]() { handleFocuserPosition(); });
    server->on("/api/v1/focuser/0/stepsize", HTTP_GET, [this]() { handleFocuserStepSize(); });
    server->on("/api/v1/focuser/0/tempcomp", HTTP_GET, [this]() { handleFocuserTempComp(); });
    server->on("/api/v1/focuser/0/tempcomp", HTTP_PUT, [this]() { handleFocuserTempComp(); });
    server->on("/api/v1/focuser/0/tempcompavailable", HTTP_GET, [this]() { handleFocuserTempCompAvailable(); });
    server->on("/api/v1/focuser/0/temperature", HTTP_GET, [this]() { handleFocuserTemperature(); });
    
    server->on("/api/v1/focuser/0/halt", HTTP_PUT, [this]() { handleFocuserHalt(); });
    server->on("/api/v1/focuser/0/move", HTTP_PUT, [this]() { handleFocuserMove(); });
    server->on("/api/v1/focuser/0/sync", HTTP_PUT, [this]() { handleFocuserSync(); });
}

void AlpacaServer::setupRotatorAPI() {
    server->on("/api/v1/rotator/0/action", HTTP_PUT, [this]() { handleAction(); });
    server->on("/api/v1/rotator/0/commandblind", HTTP_PUT, [this]() { handleCommandBlind(); });
    server->on("/api/v1/rotator/0/commandbool", HTTP_PUT, [this]() { handleCommandBool(); });
    server->on("/api/v1/rotator/0/commandstring", HTTP_PUT, [this]() { handleCommandString(); });
    server->on("/api/v1/rotator/0/connected", HTTP_GET, [this]() { handleConnected(); });
    server->on("/api/v1/rotator/0/connected", HTTP_PUT, [this]() { handleConnected(); });
    server->on("/api/v1/rotator/0/description", HTTP_GET, [this]() { handleDescriptionDevice(); });
    server->on("/api/v1/rotator/0/driverinfo", HTTP_GET, [this]() { handleDriverInfo(); });
    server->on("/api/v1/rotator/0/driverversion", HTTP_GET, [this]() { handleDriverVersion(); });
    server->on("/api/v1/rotator/0/interfaceversion", HTTP_GET, [this]() { handleInterfaceVersion(); });
    server->on("/api/v1/rotator/0/name", HTTP_GET, [this]() { handleName(); });
    server->on("/api/v1/rotator/0/supportedactions", HTTP_GET, [this]() { handleSupportedActions(); });

    server->on("/api/v1/rotator/0/ismoving", HTTP_GET, [this]() { handleRotatorIsMoving(); });
    server->on("/api/v1/rotator/0/position", HTTP_GET, [this]() { handleRotatorPosition(); });
    server->on("/api/v1/rotator/0/targetposition", HTTP_GET, [this]() { handleRotatorTargetPosition(); });
    server->on("/api/v1/rotator/0/stepsize", HTTP_GET, [this]() { handleRotatorStepSize(); });
    server->on("/api/v1/rotator/0/canreverse", HTTP_GET, [this]() { handleRotatorCanReverse(); });
    server->on("/api/v1/rotator/0/mechanicalposition", HTTP_GET, [this]() { handleRotatorMechanicalPosition(); });
    server->on("/api/v1/rotator/0/halt", HTTP_PUT, [this]() { handleRotatorHalt(); });
    server->on("/api/v1/rotator/0/move", HTTP_PUT, [this]() { handleRotatorMove(); });
    server->on("/api/v1/rotator/0/moveabsolute", HTTP_PUT, [this]() { handleRotatorMoveAbsolute(); });
    server->on("/api/v1/rotator/0/movemechanical", HTTP_PUT, [this]() { handleRotatorMoveMechanical(); });
    server->on("/api/v1/rotator/0/sync", HTTP_PUT, [this]() { handleRotatorSync(); });
    
    // Rotator-specific Reverse property (optional but common)
    server->on("/api/v1/rotator/0/reverse", HTTP_GET, [this]() { handleRotatorReverse(); });
    server->on("/api/v1/rotator/0/reverse", HTTP_PUT, [this]() { handleRotatorReverse(); });
}

// Implement Common Device Endpoints (Stubbed)
void AlpacaServer::handleAction() { handleNotFound(); }
void AlpacaServer::handleCommandBlind() { handleNotFound(); }
void AlpacaServer::handleCommandBool() { handleNotFound(); }
void AlpacaServer::handleCommandString() { handleNotFound(); }

void AlpacaServer::handleConnected() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    
    if (server->method() == HTTP_PUT) {
        if (!server->hasArg("Connected")) {
            server->send(400, "text/plain", "Bad Request");
            return;
        }
        String val = server->arg("Connected");
        val.toLowerCase();
        if (val != "true" && val != "false") {
            server->send(400, "text/plain", "Bad Request");
            return;
        }
        
        isConnected = (val == "true");
    } else {
        doc["Value"] = isConnected;
    }
    
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleDescriptionDevice() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = isRotator ? "AstroK Rotator" : "AstroK Focuser";
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleDriverInfo() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = "AstroK Alpaca Driver";
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleDriverVersion() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = FW_VERSION;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleInterfaceVersion() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = 3;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleName() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = isRotator ? "AstroRotator" : "AstroFocuser";
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleSupportedActions() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"].to<JsonArray>(); // Empty array
    sendJsonResponse(doc, clientID, clientTransID);
}

// ---------------------------------------------------------
// Focuser Implementation
// ---------------------------------------------------------
void AlpacaServer::handleFocuserAbsolute() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = true; // Stepper is an absolute position device
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserIsMoving() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = (stepper.distanceToGo() != 0);
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserMaxIncrement() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = maxStepsGlobal;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserMaxStep() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = maxStepsGlobal;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserPosition() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = stepper.currentPosition();
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserStepSize() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = 1.0; // Assume 1 micron or arbitrary unit
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserTempComp() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    if (server->method() == HTTP_PUT) {
        if (!server->hasArg("TempComp")) {
             server->send(400, "text/plain", "Bad Request");
             return;
        }
        String val = server->arg("TempComp");
        val.toLowerCase();
        if(val != "true" && val != "false") {
             server->send(400, "text/plain", "Bad Request");
             return;
        }
        // Not implemented error
        sendJsonResponse(doc, clientID, clientTransID, 0x400, "Not implemented");
        return;
    }
    doc["Value"] = false;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserTempCompAvailable() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = false;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserTemperature() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    // El dispositivo no lleva sensor de temperatura. ASCOM espera el error
    // NotImplemented (0x400) en ese caso, no un valor inventado.
    sendJsonResponse(doc, clientID, clientTransID, 0x400, "Not implemented");
}

void AlpacaServer::handleFocuserHalt() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    
    stepper.stop();
    targetPos = stepper.currentPosition();
    
    JsonDocument doc;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserMove() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;

    if (!server->hasArg("Position")) {
        server->send(400, "text/plain", "Bad Request");
        return;
    }
    String posArg = server->arg("Position");

    // Comprobar si es numérico válido
    for(int i = 0; i < posArg.length(); i++) {
        if(!isDigit(posArg[i]) && posArg[i] != '-' && posArg[i] != '+') {
            server->send(400, "text/plain", "Bad Request");
            return;
        }
    }

    long newPos = posArg.toInt();
    if (newPos < 0 || newPos > maxStepsGlobal) {
        sendJsonResponse(doc, clientID, clientTransID, 0x401, "Invalid Value");
        return;
    }

    targetPos = newPos;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFocuserSync() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;

    if (!server->hasArg("Position")) {
        server->send(400, "text/plain", "Bad Request");
        return;
    }
    String posArg = server->arg("Position");

    for(int i = 0; i < posArg.length(); i++) {
        if(!isDigit(posArg[i]) && posArg[i] != '-' && posArg[i] != '+') {
            server->send(400, "text/plain", "Bad Request");
            return;
        }
    }

    long newPos = posArg.toInt();
    stepper.setCurrentPosition(newPos);
    targetPos = newPos;

    sendJsonResponse(doc, clientID, clientTransID);
}

// ---------------------------------------------------------
// Rotator Implementation
// ---------------------------------------------------------

void AlpacaServer::handleRotatorIsMoving() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = (stepper.distanceToGo() != 0);
    sendJsonResponse(doc, clientID, clientTransID);
}

// Converts physical step position to degrees (0-360)
float AlpacaServer::getRotatorDegrees(long position) {
    long offset = position - POSICION_CENTRAL;
    float degrees = (float)offset / PASOS_POR_GRADO;
    
    while (degrees < 0) degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;
    
    // Evitar valores extremadamente cercanos a 360 por precisión de float
    if (degrees >= 359.999) degrees = 0.0;
    
    return degrees;
}

void AlpacaServer::handleRotatorPosition() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = getRotatorDegrees(stepper.currentPosition());
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorTargetPosition() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = getRotatorDegrees(targetPos);
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorStepSize() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = 1.0 / PASOS_POR_GRADO;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorCanReverse() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = true; // We support reversing
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorMechanicalPosition() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    // For many simple rotators, MechanicalPosition is the same as Position
    doc["Value"] = getRotatorDegrees(stepper.currentPosition());
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorHalt() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    
    stepper.stop();
    targetPos = stepper.currentPosition();
    
    JsonDocument doc;
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorMove() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;

    if (!server->hasArg("Position")) {
        server->send(400, "text/plain", "Bad Request");
        return;
    }
    String posArg = server->arg("Position");

    for(int i = 0; i < posArg.length(); i++) {
        if(!isDigit(posArg[i]) && posArg[i] != '-' && posArg[i] != '+' && posArg[i] != '.') {
            server->send(400, "text/plain", "Bad Request");
            return;
        }
    }

    float moveDeg = posArg.toFloat();
    // Normalizar movimiento relativo a una sola vuelta (-360, 360)
    moveDeg = fmod(moveDeg, 360.0);
    
    float currentDeg = getRotatorDegrees(stepper.currentPosition());
    float targetDeg = currentDeg + moveDeg;
    
    // Normalizar target final al rango [0, 360)
    while (targetDeg >= 360.0) targetDeg -= 360.0;
    while (targetDeg < 0.0) targetDeg += 360.0;
    
    float physicalTargetSteps = targetDeg * PASOS_POR_GRADO;

    targetPos = (long)round(physicalTargetSteps);
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorMoveAbsolute() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;

    if (!server->hasArg("Position")) {
        server->send(400, "text/plain", "Bad Request");
        return;
    }
    String posArg = server->arg("Position");

    for(int i = 0; i < posArg.length(); i++) {
        if(!isDigit(posArg[i]) && posArg[i] != '-' && posArg[i] != '+' && posArg[i] != '.') {
            server->send(400, "text/plain", "Bad Request");
            return;
        }
    }

    float targetLogicalDeg = posArg.toFloat();
    // Normalizar target a [0, 360)
    targetLogicalDeg = fmod(targetLogicalDeg, 360.0);
    if (targetLogicalDeg < 0) targetLogicalDeg += 360.0;

    // Convertir target lógico a posición física absoluta
    float physicalTargetSteps = targetLogicalDeg * PASOS_POR_GRADO;
    
    targetPos = (long)round(physicalTargetSteps);

    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorMoveMechanical() {
    // Para simplificar, tratamos MoveMechanical igual que MoveAbsolute
    handleRotatorMoveAbsolute();
}

void AlpacaServer::handleRotatorSync() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;

    if (!server->hasArg("Position")) {
        server->send(400, "text/plain", "Bad Request");
        return;
    }
    String posArg = server->arg("Position");
    float newPosDeg = posArg.toFloat();
    
    // Normalizar posición a [0, 360)
    newPosDeg = fmod(newPosDeg, 360.0);
    if (newPosDeg < 0) newPosDeg += 360.0;

    // Ajustar la posición actual del motor para que getRotatorDegrees() devuelva newPosDeg
    // Reverse ya no toca los grados: se aplica invirtiendo el pin DIR
    // (aplicarSentido en main.cpp), así que aquí la cuenta es directa.
    long newSteps = POSICION_CENTRAL + (long)round(newPosDeg * PASOS_POR_GRADO);
    
    stepper.setCurrentPosition(newSteps);
    targetPos = newSteps;

    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleRotatorReverse() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    if (server->method() == HTTP_PUT) {
        if (!server->hasArg("Reverse")) {
             server->send(400, "text/plain", "Bad Request");
             return;
        }
        String val = server->arg("Reverse");
        val.toLowerCase();
        if(val != "true" && val != "false") {
             server->send(400, "text/plain", "Bad Request");
             return;
        }
        // Se guarda en la config de NVS, que es la fuente única: así lo ven
        // igual la web, el driver ASCOM por USB y el siguiente arranque.
        // aplicarSentido() lo pasa al pin DIR y actualiza rotatorReverse.
        JsonDocument cfg;
        deserializeJson(cfg, preferences.getString("config", "{}"));
        cfg["rotator"]["reverse"] = (val == "true");
        String outStr;
        serializeJson(cfg, outStr);
        preferences.putString("config", outStr);
        if (configAppliedCallback) configAppliedCallback();
    } else {
        doc["Value"] = rotatorReverse;
    }
    sendJsonResponse(doc, clientID, clientTransID);
}

void AlpacaServer::handleFirmwareVersion() {
    uint32_t clientID = getClientID();
    uint32_t clientTransID = getClientTransactionID();
    JsonDocument doc;
    doc["Value"] = FW_VERSION;
    sendJsonResponse(doc, clientID, clientTransID);
}

// ---------------------------------------------------------
// Web Setup Implementation
// ---------------------------------------------------------

void AlpacaServer::setupWebConfigAPI() {
    // Endpoints oficiales de Alpaca para setup
    server->on("/setup/v1/focuser/0/setup", HTTP_GET, [this]() { handleFileRead("/setup.html"); });
    server->on("/setup/v1/rotator/0/setup", HTTP_GET, [this]() { handleFileRead("/setup.html"); });
    
    // Alias amigables para el navegador web
    server->on("/", HTTP_GET, [this]() { handleFileRead("/setup.html"); });
    server->on("/setup", HTTP_GET, [this]() { handleFileRead("/setup.html"); });
    
    server->on("/api/setup/config", HTTP_GET, [this]() { handleGetConfig(); });
    server->on("/api/setup/config", HTTP_POST, [this]() { handleSetConfig(); });
    server->on("/api/setup/factoryreset", HTTP_POST, [this]() { handleFactoryReset(); });
    server->on("/api/setup/reset", HTTP_POST, [this]() { handleResetPosition(); });
    server->on("/api/setup/wifiscan", HTTP_GET, [this]() { handleWifiScan(); });
    
    server->on("/status", HTTP_GET, [this]() { handleStatus(); });

    // Rest of the specific handlers are removed as they are covered by handleNotFound/handleFileRead
}

void AlpacaServer::handleNotFound() {
    if (handleFileRead(server->uri())) {
        return;
    }
    server->send(404, "text/plain", "Not Found");
}

bool AlpacaServer::handleFileRead(String path) {
    if (path.endsWith("/")) {
        path += "setup.html";
    }
    
    // Special case for favicon.ico mapped to LogoAstroK.svg
    if (path == "/favicon.ico") {
        path = "/LogoAstroK.svg";
    }
    // La actualización por USB (OTA) solo cambia el firmware, no LittleFS: un
    // equipo actualizado así conserva la web con los nombres de antes del
    // cambio de marca (LogoAstroKeeper.svg, astrokeeper.css). Su setup.html
    // también es el viejo y pide esos nombres, así que solo hace falta cubrir
    // las rutas que pone el propio firmware, como la del favicon de arriba.
    if (path == "/LogoAstroK.svg" && !LittleFS.exists(path)) {
        path = "/LogoAstroKeeper.svg";
    }

    String contentType = "text/plain";
    if (path.endsWith(".html")) contentType = "text/html";
    else if (path.endsWith(".css")) contentType = "text/css";
    else if (path.endsWith(".js")) contentType = "application/javascript";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".gif")) contentType = "image/gif";
    else if (path.endsWith(".jpg") || path.endsWith(".jpeg")) contentType = "image/jpeg";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";
    else if (path.endsWith(".xml")) contentType = "text/xml";
    else if (path.endsWith(".pdf")) contentType = "application/pdf";
    else if (path.endsWith(".zip")) contentType = "application/zip";
    else if (path.endsWith(".json")) contentType = "application/json";
    else if (path.endsWith(".svg")) contentType = "image/svg+xml";

    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server->sendHeader("Cache-Control", "max-age=7200");
        server->streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

void AlpacaServer::handleGetConfig() {
    String configJson = preferences.getString("config", "{}");
    
    JsonDocument doc;
    deserializeJson(doc, configJson);
    
    // Inyectar el modo actual basado en el hardware
    doc["isRotator"] = isRotator;
    
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void AlpacaServer::handleSetConfig() {
    if (server->hasArg("plain") == false) {
        server->send(400, "text/plain", "Body not received");
        return;
    }
    
    // Validar JSON entrante
    JsonDocument newDoc;
    DeserializationError error = deserializeJson(newDoc, server->arg("plain"));
    
    if (error) {
        server->send(400, "text/plain", "Invalid JSON");
        return;
    }

    // Leer el actual
    String currentJson = preferences.getString("config", "{}");
    JsonDocument currentDoc;
    deserializeJson(currentDoc, currentJson);

    // Detectar cambios en parámetros avanzados
    bool advancedParamsChanged = false;
    if (!newDoc["focuser"].isNull() && !currentDoc["focuser"].isNull()) {
        if (newDoc["focuser"]["current_ma"] != currentDoc["focuser"]["current_ma"] ||
            newDoc["focuser"]["microsteps"] != currentDoc["focuser"]["microsteps"] ||
            newDoc["focuser"]["hold_current_ma"] != currentDoc["focuser"]["hold_current_ma"] ||
            newDoc["focuser"]["stall_threshold"] != currentDoc["focuser"]["stall_threshold"]) {
            advancedParamsChanged = true;
        }
    }
    if (!newDoc["rotator"].isNull() && !currentDoc["rotator"].isNull()) {
        if (newDoc["rotator"]["current_ma"] != currentDoc["rotator"]["current_ma"] ||
            newDoc["rotator"]["microsteps"] != currentDoc["rotator"]["microsteps"] ||
            newDoc["rotator"]["hold_current_ma"] != currentDoc["rotator"]["hold_current_ma"] ||
            newDoc["rotator"]["stall_threshold"] != currentDoc["rotator"]["stall_threshold"]) {
            advancedParamsChanged = true;
        }
    }

    if (advancedParamsChanged) {
        uint32_t changes = preferences.getUInt("adv_changes", 0);
        preferences.putUInt("adv_changes", changes + 1);
    }

    // Mezclar los cambios de newDoc a currentDoc
    for (JsonPair kv : newDoc.as<JsonObject>()) {
        currentDoc[kv.key()] = kv.value();
    }

    // Escribir el archivo en NVS
    String outStr;
    serializeJson(currentDoc, outStr);
    size_t written = preferences.putString("config", outStr);

    if (written == 0) {
        server->send(500, "text/plain", "Failed to save config in NVS");
        return;
    }

    // Aplicar lo que se pueda sin reiniciar (pasos por grado, velocidad,
    // aceleración, corriente, micropasos, stall). Guardar en NVS y seguir
    // usando en RAM los valores viejos es lo que hacía que pedir 180 grados
    // acabara mostrando 183: el driver ASCOM ya había releído los nuevos
    // pasos por grado del equipo y el firmware aún contaba con los anteriores.
    if (configAppliedCallback) configAppliedCallback();

    server->send(200, "application/json", "{\"status\":\"ok\", \"message\":\"Config saved.\"}");
}

void AlpacaServer::handleFactoryReset() {
    preferences.remove("config");
    server->send(200, "application/json", "{\"status\":\"ok\", \"message\":\"Config erased. Rebooting...\"}");
    delay(500);
    ESP.restart();
}

void AlpacaServer::handleResetPosition() {
    if (isRotator) {
        stepper.setCurrentPosition(POSICION_CENTRAL);
        targetPos = POSICION_CENTRAL;
    } else {
        stepper.setCurrentPosition(0);
        targetPos = 0;
    }
    
    server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void AlpacaServer::handleStatus() {
    JsonDocument doc;
    
    // Hardware & System
    JsonObject sys = doc["system"].to<JsonObject>();
    sys["firmware_version"] = FW_VERSION;
    sys["uptime_ms"] = millis();
    sys["free_heap"] = ESP.getFreeHeap();
    sys["device_mode"] = isRotator ? "Rotator" : "Focuser";
    
    // State
    JsonObject state = doc["state"].to<JsonObject>();
    state["current_position"] = stepper.currentPosition();
    state["target_position"] = targetPos;
    state["distance_to_go"] = stepper.distanceToGo();
    state["is_moving"] = (stepper.distanceToGo() != 0);
    
    if (isRotator) {
        state["current_angle"] = getRotatorDegrees(stepper.currentPosition());
        state["target_angle"] = getRotatorDegrees(targetPos);
    }
    
    // Config from NVS
    String configJson = preferences.getString("config", "{}");
    JsonDocument cfgDoc;
    deserializeJson(cfgDoc, configJson);
    doc["nvs_config"] = cfgDoc;
    
    // Counters
    doc["adv_changes"] = preferences.getUInt("adv_changes", 0);
    
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}


void AlpacaServer::handleWifiScan() {
    int n = WiFi.scanComplete();
    
    if (n == WIFI_SCAN_FAILED) {
        // Start a new scan asynchronously
        WiFi.scanNetworks(true);
        server->send(202, "application/json", "{\"status\":\"scanning\"}");
        return;
    } 
    
    if (n == WIFI_SCAN_RUNNING) {
        server->send(202, "application/json", "{\"status\":\"scanning\"}");
        return;
    }
    
    // Scan is complete
    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();
    
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["encryption"] = WiFi.encryptionType(i);
        }
    }
    WiFi.scanDelete();
    
    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}
