#include <Arduino.h>
#include <NimBLEDevice.h>

// UUIDs del Nordic UART Service, que entienden casi todas las apps de prueba.
#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static NimBLECharacteristic *txChar = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, ble_gap_conn_desc *desc) override {
        Serial.printf("Conectado: %s\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
    void onDisconnect(NimBLEServer *server) override {
        Serial.println("Desconectado, vuelvo a anunciar");
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        Serial.printf("RX: %s\n", v.c_str());
        txChar->setValue(v);
        txChar->notify();
    }
};

void setup() {
    Serial.begin(115200);
    delay(1500);

    char nombre[20];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(nombre, sizeof(nombre), "AstroK-BLE-%06X", (uint32_t)(mac >> 24) & 0xFFFFFF);

    NimBLEDevice::init(nombre);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    NimBLEService *svc = server->createService(NUS_SERVICE);
    txChar = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *rx = svc->createCharacteristic(NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new RxCallbacks());
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE);
    adv->setScanResponse(true);
    adv->start();

    Serial.printf("Anunciando como %s (%s)\n", nombre, NimBLEDevice::getAddress().toString().c_str());
}

void loop() {
    static uint32_t t = 0;
    if (millis() - t > 5000) {
        t = millis();
        Serial.printf("vivo, clientes: %d\n", NimBLEDevice::getServer()->getConnectedCount());
    }
}
