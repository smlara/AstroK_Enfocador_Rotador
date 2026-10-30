#include "BleLink.h"
#include <NimBLEDevice.h>

BleLink bleLink;

#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Un trozo de actualización (:UD#) son unos 1400 caracteres, y el driver no
// manda el siguiente hasta tener el OK: con esto cabe uno entero con margen.
#define RX_TAM 4096

static uint8_t rxBuf[RX_TAM];
static volatile size_t rxCabeza = 0; // donde escribe la tarea de NimBLE
static volatile size_t rxCola = 0;   // donde lee loop()
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

static NimBLECharacteristic* txChar = nullptr;
static volatile uint16_t conexion = BLE_HS_CONN_HANDLE_NONE;
static volatile long pin = -1;
static bool iniciado = false;
static String txPendiente;

static void vaciarRx() {
    portENTER_CRITICAL(&rxMux);
    rxCabeza = rxCola = 0;
    portEXIT_CRITICAL(&rxMux);
}

class ServidorCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
        vaciarRx();
        conexion = desc->conn_handle;
    }
    // NimBLE vuelve a anunciar solo al desconectarse (advertiseOnDisconnect).
    void onDisconnect(NimBLEServer*) override {
        conexion = BLE_HS_CONN_HANDLE_NONE;
        pin = -1;
    }
    // La pila pide la clave que va a enseñar (somos DISPLAY_ONLY). Una nueva
    // en cada intento: una fija se acaba sabiendo, y no hay teclado para
    // cambiarla. loop() la pinta, no esta tarea: la OLED no es reentrante.
    uint32_t onPassKeyRequest() override {
        uint32_t clave = esp_random() % 1000000;
        pin = (long)clave;
        return clave;
    }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        pin = -1;
        if (!desc->sec_state.encrypted || !desc->sec_state.authenticated)
            NimBLEDevice::getServer()->disconnect(desc->conn_handle);
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    // Corre en la tarea de NimBLE. Si el buffer se llena, lo que sobra se
    // pierde: el comando llega cortado, y el ':' del siguiente resincroniza.
    void onWrite(NimBLECharacteristic* c) override {
        NimBLEAttValue v = c->getValue();
        portENTER_CRITICAL(&rxMux);
        for (size_t i = 0; i < v.length(); i++) {
            size_t sig = (rxCabeza + 1) % RX_TAM;
            if (sig == rxCola) break;
            rxBuf[rxCabeza] = v.data()[i];
            rxCabeza = sig;
        }
        portEXIT_CRITICAL(&rxMux);
    }
};

void BleLink::begin(const String& nombre, bool rotador) {
    NimBLEDevice::init(nombre.c_str());
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    // Vínculo guardado, protección MITM (por eso la clave) y LE Secure
    // Connections. Sin teclado: el equipo solo enseña el PIN.
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    // Lo que se pide; el cliente decide. Con el de por defecto (23) cada
    // notificación lleva 20 bytes y :CG# necesitaría decenas.
    NimBLEDevice::setMTU(247);

    NimBLEServer* servidor = NimBLEDevice::createServer();
    servidor->setCallbacks(new ServidorCallbacks());
    NimBLEService* svc = servidor->createService(NUS_SERVICE);
    txChar = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic* rx = svc->createCharacteristic(
        NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
    rx->setCallbacks(new RxCallbacks());
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE);
    // 0xFFFF es el identificador de fabricante reservado para pruebas: no
    // hay uno propio. Cabe junto al UUID (3 + 18 + 7 de 31 bytes).
    adv->setManufacturerData(std::string("\xFF\xFF" "AK") + (rotador ? 'R' : 'F'));
    // El nombre no cabe junto al UUID de 128 bits en el anuncio: va en la
    // respuesta al escaneo.
    adv->setScanResponse(true);
    adv->start();
    iniciado = true;
}

bool BleLink::activo() const {
    return iniciado;
}

long BleLink::pinPendiente() const {
    return pin;
}

void BleLink::olvidarEmparejamientos() {
    if (iniciado) NimBLEDevice::deleteAllBonds();
}

bool BleLink::conectado() const {
    return conexion != BLE_HS_CONN_HANDLE_NONE;
}

int BleLink::available() {
    portENTER_CRITICAL(&rxMux);
    size_t n = (rxCabeza + RX_TAM - rxCola) % RX_TAM;
    portEXIT_CRITICAL(&rxMux);
    return (int)n;
}

int BleLink::peek() {
    int c = -1;
    portENTER_CRITICAL(&rxMux);
    if (rxCola != rxCabeza) c = rxBuf[rxCola];
    portEXIT_CRITICAL(&rxMux);
    return c;
}

int BleLink::read() {
    int c = -1;
    portENTER_CRITICAL(&rxMux);
    if (rxCola != rxCabeza) {
        c = rxBuf[rxCola];
        rxCola = (rxCola + 1) % RX_TAM;
    }
    portEXIT_CRITICAL(&rxMux);
    return c;
}

size_t BleLink::write(uint8_t c) {
    txPendiente += (char)c;
    if (c == '#') flush();
    return 1;
}

size_t BleLink::write(const uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) write(buf[i]);
    return n;
}

// Se manda en trozos del MTU negociado. NimBLECharacteristic::notify() no
// sirve aquí: corta lo que no cabe en un paquete y, si la pila se queda sin
// buffers (varias notificaciones seguidas, como en :CG#), descarta el envío
// sin avisar. Por eso se llama directamente a la pila y se reintenta.
void BleLink::flush() {
    uint16_t con = conexion;
    // Sin cifrar no llega ningún comando (RX lo exige), pero se comprueba
    // también aquí para no mandar nada a quien no se ha emparejado.
    ble_gap_conn_desc desc;
    if (con == BLE_HS_CONN_HANDLE_NONE || txChar == nullptr ||
        ble_gap_conn_find(con, &desc) != 0 || !desc.sec_state.encrypted) {
        txPendiente = "";
        return;
    }

    size_t trozo = NimBLEDevice::getServer()->getPeerMTU(con);
    trozo = (trozo > 3) ? trozo - 3 : 20;
    if (trozo > 244) trozo = 244;

    const uint8_t* datos = (const uint8_t*)txPendiente.c_str();
    size_t total = txPendiente.length();
    for (size_t hecho = 0; hecho < total; ) {
        size_t n = min(trozo, total - hecho);
        int rc = BLE_HS_ENOMEM;
        for (int intento = 0; intento < 200 && conexion == con; intento++) {
            // La pila consume el mbuf aunque falle: hay que crearlo cada vez.
            os_mbuf* om = ble_hs_mbuf_from_flat(datos + hecho, n);
            rc = om ? ble_gattc_notify_custom(con, txChar->getHandle(), om) : BLE_HS_ENOMEM;
            if (rc != BLE_HS_ENOMEM) break;
            delay(2);
        }
        if (rc != 0) break; // desconectado o error de la pila: se abandona
        hecho += n;
    }
    txPendiente = "";
}
