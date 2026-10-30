#include "astrok_comun.h"
#include "astrok_ble.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <strings.h>
#include <glob.h>
#include <indilogger.h>
#include <iterator>
#include <regex>

namespace
{
// Claves de la sección del modo que van en ASTROK_MOTOR, en orden.
struct ClaveMotor
{
    const char *elemento;
    const char *clave;
    const char *etiqueta;
    double minimo, maximo, paso;
};

const ClaveMotor CLAVES_MOTOR[] =
{
    { "CURRENT_MA",      "current_ma",      "Run current (mA)",      0, 2000,  50 },
    { "HOLD_CURRENT_MA", "hold_current_ma", "Hold current (mA, 0 = off)", 0, 2000, 50 },
    { "MICROSTEPS",      "microsteps",      "Microsteps (1 = full)", 1, 256,   1 },
    { "MAX_SPEED",       "max_speed",       "Max speed (steps/s)",   1, 50000, 100 },
    { "ACCELERATION",    "acceleration",    "Acceleration (steps/s²)", 1, 50000, 100 },
    { "STALL_THRESHOLD", "stall_threshold", "Stall threshold (0 = off)", 0, 255, 1 },
    { "STALL_MIN_SPEED", "stall_min_speed", "Stall min speed (steps/s)", 0, 50000, 50 },
};

const char *TAB_EQUIPO = "Device";
const char *TAB_MOTOR = "Motor";
const char *TAB_INALAMBRICO = "Wireless";
const char *TAB_MANTENIMIENTO = "Maintenance";

// Tamaños de partición de la tabla por defecto (Firmware.cs).
const size_t TAMANO_MAXIMO_APP = 0x140000;
const size_t TAMANO_MAXIMO_WEB = 0x160000;

long long ahoraMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void dormir(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

double numero(const nlohmann::json &s, const char *clave, double defecto)
{
    auto it = s.find(clave);
    return (it != s.end() && it->is_number()) ? it->get<double>() : defecto;
}

std::string texto(const nlohmann::json &s, const char *clave)
{
    auto it = s.find(clave);
    return (it != s.end() && it->is_string()) ? it->get<std::string>() : "";
}

bool booleano(const nlohmann::json &s, const char *clave, bool defecto)
{
    auto it = s.find(clave);
    return (it != s.end() && it->is_boolean()) ? it->get<bool>() : defecto;
}

bool pinValido(const std::string &p)
{
    return p.size() == 4 && std::all_of(p.begin(), p.end(), ::isdigit);
}
}

AstroKComun::AstroKComun(INDI::DefaultDevice &d, bool esRotador) : dev(d), rotador(esRotador)
{
}

AstroKComun::~AstroKComun()
{
    if (hilo.joinable())
        hilo.join();
}

void AstroKComun::info(const std::string &t)
{
    DEBUGFDEVICE(dev.getDeviceName(), INDI::Logger::DBG_SESSION, "%s", t.c_str());
}

void AstroKComun::aviso(const std::string &t)
{
    DEBUGFDEVICE(dev.getDeviceName(), INDI::Logger::DBG_WARNING, "%s", t.c_str());
}

void AstroKComun::error(const std::string &t)
{
    DEBUGFDEVICE(dev.getDeviceName(), INDI::Logger::DBG_ERROR, "%s", t.c_str());
}

nlohmann::json &AstroKComun::seccion()
{
    nlohmann::json &s = cfg[rotador ? "rotator" : "focuser"];
    if (!s.is_object())
        s = nlohmann::json::object();
    return s;
}

// =========================================================================
// Propiedades
// =========================================================================

void AstroKComun::initProperties()
{
    const char *nombre = dev.getDeviceName();

    // ---- Conexión ----
    TransporteSP[USB].fill("USB", "USB", ISS_ON);
    TransporteSP[BLE].fill("BLE", "Bluetooth LE", ISS_OFF);
    TransporteSP.fill(nombre, "ASTROK_TRANSPORT", "Link", CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    PuertoTP[0].fill("PORT", "Port", "/dev/ttyACM0");
    PuertoTP.fill(nombre, "ASTROK_USB_PORT", "USB port", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);

    BuscarUsbSP[0].fill("SEARCH", "Search", ISS_OFF);
    BuscarUsbSP.fill(nombre, "ASTROK_USB_SEARCH", "Find on USB", CONNECTION_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    BleTP[DIRECCION].fill("ADDRESS", "Address", "");
    BleTP[PIN].fill("PIN", "PIN (4 digits)", "");
    BleTP.fill(nombre, "ASTROK_BLE", "Bluetooth", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);

    BuscarBleSP[0].fill("SCAN", "Scan", ISS_OFF);
    BuscarBleSP.fill(nombre, "ASTROK_BLE_SCAN", "Find on Bluetooth", CONNECTION_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    EncontradosTP[0].fill("DEVICES", "Devices", "");
    EncontradosTP.fill(nombre, "ASTROK_BLE_FOUND", "Found", CONNECTION_TAB, IP_RO, 60, IPS_IDLE);

    // ---- Equipo ----
    InfoTP[FIRMWARE].fill("FIRMWARE", "Firmware", "");
    InfoTP[HARDWARE].fill("HARDWARE", "Motor; chip", "");
    InfoTP[ENLACE].fill("LINK", "Link", "");
    InfoTP[RED].fill("NETWORK", "WiFi", "");
    InfoTP.fill(nombre, "ASTROK_INFO", "Device", TAB_EQUIPO, IP_RO, 60, IPS_IDLE);

    DiagTP[0].fill("STATUS", "Status", "");
    DiagTP.fill(nombre, "ASTROK_DRIVER_STATUS", "Motor driver", TAB_EQUIPO, IP_RO, 60, IPS_IDLE);
    DiagSP[0].fill("REFRESH", "Refresh", ISS_OFF);
    DiagSP.fill(nombre, "ASTROK_DRIVER_STATUS_REFRESH", "Motor driver", TAB_EQUIPO, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // ---- Motor ----
    for (int i = 0; i < 7; i++)
    {
        const ClaveMotor &c = CLAVES_MOTOR[i];
        MotorNP[i].fill(c.elemento, c.etiqueta, "%.0f", c.minimo, c.maximo, c.paso, 0);
    }
    MotorNP.fill(nombre, "ASTROK_MOTOR", "Motor", TAB_MOTOR, IP_RW, 60, IPS_IDLE);

    PasosGradoNP[0].fill("STEPS", "Steps per degree", "%.4f", 0.01, 100000, 1, 37.75);
    PasosGradoNP.fill(nombre, "ASTROK_STEPS_PER_DEGREE", "Gearing", TAB_MOTOR, IP_RW, 60, IPS_IDLE);

    // ---- Inalámbrico ----
    RadiosSP[WIFI].fill("WIFI", "WiFi", ISS_ON);
    RadiosSP[BT].fill("BLE", "Bluetooth", ISS_ON);
    RadiosSP.fill(nombre, "ASTROK_RADIOS", "Radios", TAB_INALAMBRICO, IP_RW, ISR_NOFMANY, 60, IPS_IDLE);

    ApTP[0].fill("NAME", "Name", "");
    ApTP[1].fill("PASSWORD", "Password", "");
    ApTP.fill(nombre, "ASTROK_WIFI_AP", "Access point", TAB_INALAMBRICO, IP_RW, 60, IPS_IDLE);

    WifiNP[0].fill("TIMEOUT_MS", "Timeout per try (ms)", "%.0f", 100, 60000, 100, 1000);
    WifiNP[1].fill("RETRIES", "Tries", "%.0f", 0, 100, 1, 10);
    WifiNP.fill(nombre, "ASTROK_WIFI_STA", "Join network", TAB_INALAMBRICO, IP_RW, 60, IPS_IDLE);

    static char nombres[REDES * 2][16], etiquetas[REDES * 2][24];
    for (int i = 0; i < REDES; i++)
    {
        std::snprintf(nombres[2 * i], 16, "SSID_%d", i + 1);
        std::snprintf(etiquetas[2 * i], 24, "Network %d", i + 1);
        std::snprintf(nombres[2 * i + 1], 16, "PASSWORD_%d", i + 1);
        std::snprintf(etiquetas[2 * i + 1], 24, "Password %d", i + 1);
        RedesTP[2 * i].fill(nombres[2 * i], etiquetas[2 * i], "");
        RedesTP[2 * i + 1].fill(nombres[2 * i + 1], etiquetas[2 * i + 1], "");
    }
    RedesTP.fill(nombre, "ASTROK_WIFI_NETWORKS", "Networks", TAB_INALAMBRICO, IP_RW, 60, IPS_IDLE);

    PinUsbTP[0].fill("PIN", "PIN (empty = none)", "");
    PinUsbTP.fill(nombre, "ASTROK_BLE_PIN", "Bluetooth PIN", TAB_INALAMBRICO, IP_RW, 60, IPS_IDLE);

    // ---- Mantenimiento ----
    AccionSP[REINICIAR].fill("REBOOT", "Reboot", ISS_OFF);
    AccionSP[FABRICA].fill("FACTORY_RESET", "Factory reset", ISS_OFF);
    AccionSP.fill(nombre, "ASTROK_ACTION", "Device", TAB_MANTENIMIENTO, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    FirmwareTP[0].fill("PATH", "File (astrok_<motor>_v<x.y.z>_<date>.bin)", "");
    FirmwareTP.fill(nombre, "ASTROK_FIRMWARE_FILE", "Firmware", TAB_MANTENIMIENTO, IP_RW, 60, IPS_IDLE);
    FirmwareSP[0].fill("START", "Update", ISS_OFF);
    FirmwareSP.fill(nombre, "ASTROK_FIRMWARE_UPDATE", "Firmware", TAB_MANTENIMIENTO, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);
    ProgresoNP[0].fill("PERCENT", "Progress (%)", "%.0f", 0, 100, 1, 0);
    ProgresoNP.fill(nombre, "ASTROK_FIRMWARE_PROGRESS", "Firmware", TAB_MANTENIMIENTO, IP_RO, 60, IPS_IDLE);
}

void AstroKComun::ISGetProperties()
{
    // Solo la primera vez. Una vez definidas, DefaultDevice::ISGetProperties
    // las vuelve a mandar él a cada cliente nuevo; y lo que haya en ellas es
    // lo último que se ha elegido, aunque aún no se haya guardado.
    if (configCargada)
        return;
    configCargada = true;

    dev.defineProperty(TransporteSP);
    dev.defineProperty(PuertoTP);
    dev.defineProperty(BuscarUsbSP);
    dev.defineProperty(BleTP);
    dev.defineProperty(BuscarBleSP);
    dev.defineProperty(EncontradosTP);

    if (cargarConfigIndi)
    {
        cargarConfigIndi(TransporteSP.getName());
        cargarConfigIndi(PuertoTP.getName());
        cargarConfigIndi(BleTP.getName());
    }
}

void AstroKComun::updateProperties(bool conectado)
{
    if (conectado)
    {
        dev.defineProperty(InfoTP);
        dev.defineProperty(DiagTP);
        dev.defineProperty(DiagSP);
        dev.defineProperty(MotorNP);
        if (rotador)
            dev.defineProperty(PasosGradoNP);
        dev.defineProperty(RadiosSP);
        dev.defineProperty(ApTP);
        dev.defineProperty(WifiNP);
        dev.defineProperty(RedesTP);
        // Ver y cambiar el PIN solo por USB: el firmware lo rechaza por BLE.
        if (!equipo.esBle())
            dev.defineProperty(PinUsbTP);
        dev.defineProperty(AccionSP);
        dev.defineProperty(FirmwareTP);
        dev.defineProperty(FirmwareSP);
        dev.defineProperty(ProgresoNP);
    }
    else
    {
        dev.deleteProperty(InfoTP);
        dev.deleteProperty(DiagTP);
        dev.deleteProperty(DiagSP);
        dev.deleteProperty(MotorNP);
        if (rotador)
            dev.deleteProperty(PasosGradoNP);
        dev.deleteProperty(RadiosSP);
        dev.deleteProperty(ApTP);
        dev.deleteProperty(WifiNP);
        dev.deleteProperty(RedesTP);
        dev.deleteProperty(PinUsbTP);
        dev.deleteProperty(AccionSP);
        dev.deleteProperty(FirmwareTP);
        dev.deleteProperty(FirmwareSP);
        dev.deleteProperty(ProgresoNP);
    }
}

void AstroKComun::saveConfigItems(FILE *fp)
{
    TransporteSP.save(fp);
    PuertoTP.save(fp);
    BleTP.save(fp);
    FirmwareTP.save(fp);
}

bool AstroKComun::ISNewSwitch(const char *name, ISState *states, char *names[], int n)
{
    if (TransporteSP.isNameMatch(name))
    {
        TransporteSP.update(states, names, n);
        TransporteSP.setState(IPS_OK);
        TransporteSP.apply();
        return true;
    }

    if (BuscarUsbSP.isNameMatch(name))
    {
        BuscarUsbSP.reset();
        std::string puerto = buscarUsb();
        if (puerto.empty())
        {
            aviso(std::string("No AstroK ") + (rotador ? "rotator" : "focuser") + " found on USB.");
            BuscarUsbSP.setState(IPS_ALERT);
        }
        else
        {
            PuertoTP[0].setText(puerto);
            PuertoTP.setState(IPS_OK);
            PuertoTP.apply();
            info("Found on " + puerto);
            BuscarUsbSP.setState(IPS_OK);
        }
        BuscarUsbSP.apply();
        return true;
    }

    if (BuscarBleSP.isNameMatch(name))
    {
        BuscarBleSP.reset();
        buscarBle();
        return true;
    }

    if (DiagSP.isNameMatch(name))
    {
        DiagSP.reset();
        leerDiagnostico();
        DiagSP.setState(IPS_OK);
        DiagSP.apply();
        return true;
    }

    if (RadiosSP.isNameMatch(name))
    {
        RadiosSP.update(states, names, n);
        bool wifi = RadiosSP[WIFI].getState() == ISS_ON;
        bool ble = RadiosSP[BT].getState() == ISS_ON;
        if (!ble && equipo.esBle())
            aviso("Bluetooth will be off after the next reboot: this link will no longer work. "
                  "Use USB to turn it back on.");
        nlohmann::json nueva = cfg;
        nueva["wifi"]["enabled"] = wifi;
        nueva["ble"]["enabled"] = ble;
        if (guardarConfig(nueva))
            info("Saved. WiFi and Bluetooth changes apply after a reboot (Maintenance tab).");
        rellenarPropiedades();
        RadiosSP.apply();
        return true;
    }

    if (AccionSP.isNameMatch(name))
    {
        AccionSP.update(states, names, n);
        int accion = AccionSP.findOnSwitchIndex();
        AccionSP.reset();

        if (grabando)
        {
            aviso("A firmware update is in progress.");
            AccionSP.setState(IPS_ALERT);
            AccionSP.apply();
            return true;
        }

        if (accion == FABRICA && ahoraMs() > confirmarFabricaHasta)
        {
            // Borra configuración y PIN: se pide pulsarlo dos veces.
            confirmarFabricaHasta = ahoraMs() + 10000;
            aviso("Factory reset erases the device configuration and its Bluetooth PIN. "
                  "Press it again within 10 s to confirm.");
            AccionSP.setState(IPS_BUSY);
            AccionSP.apply();
            return true;
        }

        if (accion == REINICIAR)
        {
            info("Rebooting the device. Reconnect in a few seconds.");
            equipo.reiniciar();
        }
        else if (accion == FABRICA)
        {
            confirmarFabricaHasta = 0;
            if (!equipo.valoresFabrica())
            {
                error("The device did not accept the factory reset.");
                AccionSP.setState(IPS_ALERT);
                AccionSP.apply();
                return true;
            }
            info("Factory defaults restored; the device is rebooting. Reconnect in a few seconds.");
        }
        else
        {
            AccionSP.setState(IPS_IDLE);
            AccionSP.apply();
            return true;
        }

        AccionSP.setState(IPS_OK);
        AccionSP.apply();
        equipo.cerrar();
        if (alPerderEquipo)
            alPerderEquipo(IPS_IDLE);
        return true;
    }

    if (FirmwareSP.isNameMatch(name))
    {
        FirmwareSP.reset();
        empezarActualizacion();
        return true;
    }

    return false;
}

bool AstroKComun::ISNewNumber(const char *name, double values[], char *names[], int n)
{
    if (MotorNP.isNameMatch(name))
    {
        MotorNP.setState(guardarMotor(values, names, n) ? IPS_OK : IPS_ALERT);
        rellenarPropiedades();
        MotorNP.apply();
        return true;
    }

    if (PasosGradoNP.isNameMatch(name))
    {
        double spd = values[0];
        bool ok = spd > 0 && cambiarSeccion([spd](nlohmann::json & s)
        {
            s["steps_per_degree"] = spd;
        });
        PasosGradoNP.setState(ok ? IPS_OK : IPS_ALERT);
        PasosGradoNP.apply();
        return true;
    }

    if (WifiNP.isNameMatch(name))
    {
        WifiNP.update(values, names, n);
        WifiNP.setState(guardarWifi() ? IPS_OK : IPS_ALERT);
        WifiNP.apply();
        return true;
    }

    return false;
}

bool AstroKComun::ISNewText(const char *name, char *texts[], char *names[], int n)
{
    if (PuertoTP.isNameMatch(name))
    {
        PuertoTP.update(texts, names, n);
        PuertoTP.setState(IPS_OK);
        PuertoTP.apply();
        return true;
    }

    if (BleTP.isNameMatch(name))
    {
        BleTP.update(texts, names, n);
        std::string pin = BleTP[PIN].getText();
        if (!pin.empty() && !pinValido(pin))
        {
            aviso("The Bluetooth PIN has 4 digits.");
            BleTP.setState(IPS_ALERT);
        }
        else
            BleTP.setState(IPS_OK);
        BleTP.apply();
        return true;
    }

    if (FirmwareTP.isNameMatch(name))
    {
        FirmwareTP.update(texts, names, n);
        FirmwareTP.setState(IPS_OK);
        FirmwareTP.apply();
        return true;
    }

    if (ApTP.isNameMatch(name))
    {
        ApTP.update(texts, names, n);
        std::string clave = ApTP[1].getText();
        // WPA2 pide 8 caracteres; vacía es una red abierta.
        if (!clave.empty() && clave.size() < 8)
        {
            aviso("The access point password needs at least 8 characters (or none for an open network).");
            ApTP.setState(IPS_ALERT);
        }
        else
            ApTP.setState(guardarWifi() ? IPS_OK : IPS_ALERT);
        ApTP.apply();
        return true;
    }

    if (RedesTP.isNameMatch(name))
    {
        RedesTP.update(texts, names, n);
        RedesTP.setState(guardarWifi() ? IPS_OK : IPS_ALERT);
        RedesTP.apply();
        return true;
    }

    if (PinUsbTP.isNameMatch(name))
    {
        std::string pin = texts[0] ? texts[0] : "";
        std::string r;
        bool ok;
        if (pin.empty())
            ok = equipo.consulta("BR", r) && r == "OK";
        else if (!pinValido(pin))
        {
            aviso("The Bluetooth PIN has 4 digits.");
            ok = false;
        }
        else
            ok = equipo.consulta("BP" + pin, r) && r == "OK";

        if (ok)
            info(pin.empty() ? "Bluetooth PIN cleared: the next Bluetooth connection chooses a new one."
                 : "Bluetooth PIN changed.");
        leerPinUsb();
        PinUsbTP.setState(ok ? IPS_OK : IPS_ALERT);
        PinUsbTP.apply();
        return true;
    }

    return false;
}

// =========================================================================
// Conexión
// =========================================================================

/// Puertos USB de Espressif (VID 303a). Solo se prueban esos: mandar :GV#
/// a una montura o a otro equipo cualquiera no es buena idea.
std::string AstroKComun::buscarUsb()
{
    std::vector<std::string> candidatos;
    glob_t g {};
    if (::glob("/dev/serial/by-id/*", 0, nullptr, &g) == 0)
    {
        for (size_t i = 0; i < g.gl_pathc; i++)
        {
            std::string p = g.gl_pathv[i];
            if (p.find("Espressif") != std::string::npos || p.find("303a") != std::string::npos)
                candidatos.push_back(p);
        }
    }
    ::globfree(&g);

    // Si el puerto elegido ya está abierto por este driver no se puede probar.
    if (equipo.abierto())
        return "";

    for (const auto &p : candidatos)
    {
        std::string motivo;
        Canal *c = CanalSerie::abrir(p, motivo);
        if (c == nullptr)
            continue;
        Equipo prueba;
        prueba.usar(c);
        std::string modo;
        for (int i = 0; i < 3 && modo.empty(); i++)
            prueba.consulta("DM", modo, 1000);
        if (modo == (rotador ? "ROTATOR" : "FOCUSER"))
            return p;
    }
    return "";
}

void AstroKComun::buscarBle()
{
    info("Scanning Bluetooth for 5 s...");
    BuscarBleSP.setState(IPS_BUSY);
    BuscarBleSP.apply();

    std::string motivo;
    auto lista = CanalBle::buscar(5, motivo);
    if (!motivo.empty() && lista.empty())
    {
        error(motivo);
        BuscarBleSP.setState(IPS_ALERT);
        BuscarBleSP.apply();
        return;
    }

    char miModo = rotador ? 'R' : 'F';
    std::string resumen;
    std::string primero;
    for (const auto &e : lista)
    {
        if (e.modo != miModo && e.modo != '?')
        {
            info(e.nombre + " (" + e.direccion + ") is a " + (e.modo == 'R' ? "rotator" : "focuser") + ", skipped.");
            continue;
        }
        if (primero.empty())
            primero = e.direccion;
        if (!resumen.empty())
            resumen += ", ";
        resumen += e.nombre + " " + e.direccion + " (" + std::to_string(e.rssi) + " dBm)";
    }

    EncontradosTP[0].setText(resumen.empty() ? "none" : resumen);
    EncontradosTP.setState(resumen.empty() ? IPS_ALERT : IPS_OK);
    EncontradosTP.apply();

    if (primero.empty())
    {
        aviso("No AstroK found on Bluetooth. Is it powered on, with Bluetooth enabled and not connected elsewhere?");
        BuscarBleSP.setState(IPS_ALERT);
    }
    else
    {
        // Se queda la dirección elegida si sigue en la lista; si no, la mejor señal.
        std::string actual = BleTP[DIRECCION].getText();
        if (resumen.find(actual) == std::string::npos || actual.empty())
        {
            BleTP[DIRECCION].setText(primero);
            BleTP.apply();
        }
        info("Found: " + resumen);
        BuscarBleSP.setState(IPS_OK);
    }
    BuscarBleSP.apply();
}

bool AstroKComun::abrirCanal(std::string &motivo)
{
    Canal *c = nullptr;
    if (TransporteSP[BLE].getState() == ISS_ON)
    {
        std::string dir = BleTP[DIRECCION].getText();
        if (dir.empty())
        {
            motivo = "Set the Bluetooth address, or press Scan.";
            return false;
        }
        c = CanalBle::abrir(dir, motivo);
    }
    else
        c = CanalSerie::abrir(PuertoTP[0].getText(), motivo);

    if (c == nullptr)
        return false;
    equipo.usar(c);
    return true;
}

/// Después de abrir: el PIN si es BLE, comprobar que contesta y que está en
/// el modo de este driver, y leer información y configuración.
bool AstroKComun::saludar(std::string &motivo)
{
    if (equipo.esBle())
    {
        std::string pin = BleTP[PIN].getText();
        bool definido = false;
        if (!equipo.pinDefinido(definido))
        {
            motivo = "The device does not answer over Bluetooth.";
            return false;
        }
        if (!pinValido(pin))
        {
            motivo = definido ? "Enter the device's 4-digit Bluetooth PIN (USB or the AstroK app can show it)."
                     : "This device has no Bluetooth PIN yet: enter 4 digits and they become its PIN.";
            return false;
        }
        std::string r = equipo.autenticar(pin);
        if (r != "OK")
        {
            motivo = (r == "ER:PIN") ? "Wrong Bluetooth PIN." : "The device did not accept the PIN (" + r + ").";
            return false;
        }
        if (!definido)
            info("Bluetooth PIN set on the device.");
    }

    // Abrir el USB puede reiniciar el equipo; el protocolo se atiende ya
    // durante el arranque, pero se insiste un poco.
    std::string version;
    for (int i = 0; i < 6 && version.empty(); i++)
        if (!equipo.consulta("GV", version, 1000))
            version.clear();
    if (version.empty())
    {
        motivo = "The device does not answer. Is it an AstroK?";
        return false;
    }

    std::string modo;
    equipo.consulta("DM", modo);
    if (modo == (rotador ? "FOCUSER" : "ROTATOR"))
    {
        motivo = std::string("The device started as a ") + (rotador ? "FOCUSER" : "ROTATOR") +
                 ". Check the mode jumper, or use the AstroK " + (rotador ? "Focuser" : "Rotator") + " driver.";
        return false;
    }

    std::string fw, hw, red;
    equipo.consulta("DV", fw);
    equipo.consulta("DH", hw);
    equipo.consulta("WI", red);
    InfoTP[FIRMWARE].setText(fw.empty() ? version : fw);
    InfoTP[HARDWARE].setText(hw);
    InfoTP[ENLACE].setText(equipo.esBle() ? "Bluetooth " + std::string(BleTP[DIRECCION].getText())
                           : "USB " + std::string(PuertoTP[0].getText()));
    InfoTP[RED].setText(red);

    if (!releerConfig())
    {
        motivo = "Could not read the device configuration (:CG#).";
        return false;
    }
    return true;
}

bool AstroKComun::conectar()
{
    fallosSeguidos = 0;
    std::string motivo;
    bool ok = abrirCanal(motivo) && saludar(motivo);

    // Por USB, si el puerto guardado no vale (el equipo ha cambiado de
    // ttyACM, o es la primera vez) se busca entre los de Espressif.
    if (!ok && TransporteSP[USB].getState() == ISS_ON)
    {
        equipo.cerrar();
        std::string puerto = buscarUsb();
        if (!puerto.empty() && puerto != PuertoTP[0].getText())
        {
            info("Device found on " + puerto);
            PuertoTP[0].setText(puerto);
            PuertoTP.apply();
            std::string otro;
            ok = abrirCanal(otro) && saludar(otro);
            if (ok && guardarConfigIndi)
                guardarConfigIndi();
        }
    }

    if (!ok)
    {
        equipo.cerrar();
        error(motivo);
        return false;
    }

    info(std::string("Connected over ") + (equipo.esBle() ? "Bluetooth" : "USB") +
         ", firmware " + InfoTP[FIRMWARE].getText() + ".");
    leerDiagnostico();
    if (!equipo.esBle())
        leerPinUsb();
    return true;
}

bool AstroKComun::desconectar()
{
    if (grabando)
    {
        aviso("A firmware update is in progress: wait for it to finish.");
        return false;
    }
    if (hilo.joinable())
        hilo.join();
    equipo.cerrar();
    return true;
}

bool AstroKComun::comprobarEnlace(bool consultaOk)
{
    if (consultaOk)
    {
        fallosSeguidos = 0;
        return true;
    }
    if (++fallosSeguidos < 3)
        return true;

    // El BLE se corta de vez en cuando; se intenta una vez antes de rendirse.
    aviso("The device stopped answering, reconnecting...");
    fallosSeguidos = 0;
    equipo.cerrar();
    std::string motivo;
    if (abrirCanal(motivo) && saludar(motivo))
    {
        info("Reconnected.");
        return true;
    }
    equipo.cerrar();
    error("Lost the device: " + motivo);
    return false;
}

// =========================================================================
// Configuración del equipo
// =========================================================================

bool AstroKComun::releerConfig()
{
    nlohmann::json nueva;
    if (!equipo.leerConfig(nueva))
        return false;
    cfg = nueva;
    rellenarPropiedades();
    if (alCambiarConfig)
        alCambiarConfig();
    return true;
}

bool AstroKComun::guardarConfig(const nlohmann::json &nueva)
{
    if (!equipo.guardarConfig(nueva))
    {
        error("The device rejected the configuration.");
        return false;
    }
    // Se relee en vez de dar por buena la que se ha mandado: es lo que el
    // equipo tiene de verdad.
    if (!releerConfig())
        cfg = nueva;
    return true;
}

bool AstroKComun::cambiarSeccion(const std::function<void(nlohmann::json &)> &cambio)
{
    nlohmann::json nueva = cfg;
    nlohmann::json &s = nueva[rotador ? "rotator" : "focuser"];
    if (!s.is_object())
        s = nlohmann::json::object();
    cambio(s);
    return guardarConfig(nueva);
}

void AstroKComun::rellenarPropiedades()
{
    const nlohmann::json &s = seccion();
    double corriente = numero(s, "current_ma", 0);
    for (int i = 0; i < 7; i++)
    {
        const ClaveMotor &c = CLAVES_MOTOR[i];
        double v = numero(s, c.clave, 0);
        // Configuración de firmware anterior: un booleano 'hold' en lugar de
        // corriente de reposo, que equivalía a la mitad de la de marcha.
        if (!std::strcmp(c.clave, "hold_current_ma") && !s.contains("hold_current_ma"))
            v = booleano(s, "hold", false) ? corriente / 2 : 0;
        MotorNP[i].setValue(v);
    }
    PasosGradoNP[0].setValue(numero(s, "steps_per_degree", 37.75));

    const nlohmann::json &w = cfg.contains("wifi") && cfg["wifi"].is_object() ? cfg["wifi"] : nlohmann::json::object();
    // Sin la clave, activadas: es lo que hace el firmware.
    RadiosSP[WIFI].setState(booleano(w, "enabled", true) ? ISS_ON : ISS_OFF);
    bool ble = !(cfg.contains("ble") && cfg["ble"].is_object()) || booleano(cfg["ble"], "enabled", true);
    RadiosSP[BT].setState(ble ? ISS_ON : ISS_OFF);
    ApTP[0].setText(texto(w, "ap_name"));
    ApTP[1].setText(texto(w, "ap_password"));
    WifiNP[0].setValue(numero(w, "timeout_ms", 1000));
    WifiNP[1].setValue(numero(w, "retries", 10));

    auto redes = w.find("networks");
    for (int i = 0; i < REDES; i++)
    {
        std::string ssid, clave;
        if (redes != w.end() && redes->is_array() && (size_t)i < redes->size() && (*redes)[i].is_object())
        {
            ssid = texto((*redes)[i], "ssid");
            clave = texto((*redes)[i], "password");
        }
        RedesTP[2 * i].setText(ssid);
        RedesTP[2 * i + 1].setText(clave);
    }

    if (dev.isConnected())
    {
        MotorNP.apply();
        if (rotador)
            PasosGradoNP.apply();
        RadiosSP.apply();
        ApTP.apply();
        WifiNP.apply();
        RedesTP.apply();
    }
}

bool AstroKComun::guardarMotor(double values[], char *names[], int n)
{
    nlohmann::json cambios = nlohmann::json::object();
    for (int k = 0; k < n; k++)
    {
        for (const auto &c : CLAVES_MOTOR)
        {
            if (std::strcmp(names[k], c.elemento) != 0)
                continue;
            // Enteros en el JSON: ArduinoJson no da un 8.0 por un uint16_t, y
            // el firmware usaría su valor por defecto en silencio.
            long v = std::lround(values[k]);
            if (!std::strcmp(c.clave, "microsteps") && (v < 1 || v > 256 || (v & (v - 1)) != 0))
            {
                aviso("Microsteps must be 1, 2, 4, 8, 16, 32, 64, 128 or 256.");
                return false;
            }
            cambios[c.clave] = v;
        }
    }

    bool ok = cambiarSeccion([&cambios](nlohmann::json & s)
    {
        for (auto it = cambios.begin(); it != cambios.end(); ++it)
            s[it.key()] = it.value();
        // Sustituido por hold_current_ma; si se queda, confunde a quien lo lea.
        s.erase("hold");
    });
    if (ok && cambios.contains("microsteps"))
        info("Microsteps changed: the device rescales its position, so it does not move.");
    return ok;
}

bool AstroKComun::guardarWifi()
{
    nlohmann::json nueva = cfg;
    nlohmann::json &w = nueva["wifi"];
    if (!w.is_object())
        w = nlohmann::json::object();

    w["ap_name"] = std::string(ApTP[0].getText());
    w["ap_password"] = std::string(ApTP[1].getText());
    w["timeout_ms"] = std::lround(WifiNP[0].getValue());
    w["retries"] = std::lround(WifiNP[1].getValue());

    // Las redes de las casillas, y detrás las que hubiera de más (la web
    // admite cualquier número): así no se pierden por no caber aquí.
    nlohmann::json redes = nlohmann::json::array();
    for (int i = 0; i < REDES; i++)
    {
        std::string ssid = RedesTP[2 * i].getText();
        if (!ssid.empty())
            redes.push_back({ { "ssid", ssid }, { "password", std::string(RedesTP[2 * i + 1].getText()) } });
    }
    nlohmann::json antes;
    if (cfg.contains("wifi") && cfg["wifi"].is_object() && cfg["wifi"].contains("networks"))
        antes = cfg["wifi"]["networks"];
    if (antes.is_array())
        for (size_t i = REDES; i < antes.size(); i++)
            redes.push_back(antes[i]);
    w["networks"] = redes;

    if (!guardarConfig(nueva))
        return false;
    info("WiFi settings saved. They apply after a reboot (Maintenance tab).");
    return true;
}

void AstroKComun::leerPinUsb()
{
    std::string r;
    if (equipo.consulta("BP", r))
        PinUsbTP[0].setText(r == "-" ? "" : r);
    if (dev.isConnected())
        PinUsbTP.apply();
}

void AstroKComun::leerDiagnostico()
{
    std::string r, red;
    if (equipo.consulta("DS", r))
        DiagTP[0].setText(r);
    if (equipo.consulta("WI", red))
        InfoTP[RED].setText(red);
    if (dev.isConnected())
    {
        DiagTP.apply();
        InfoTP.apply();
    }
}

// =========================================================================
// Actualización de firmware
// =========================================================================

void AstroKComun::empezarActualizacion()
{
    auto fallar = [this](const std::string & motivo)
    {
        error(motivo);
        FirmwareSP.setState(IPS_ALERT);
        FirmwareSP.apply();
    };

    if (grabando)
        return fallar("A firmware update is already in progress.");

    std::string ruta = FirmwareTP[0].getText();
    std::string nombre = ruta.substr(ruta.find_last_of('/') + 1);

    // Del nombre sale el motor para el que es: astrok_<motor>_v<x.y.z>_<aaaammdd>.bin
    static const std::regex patron("^astrok_(.+)_v(\\d+\\.\\d+\\.\\d+)_(\\d{8})\\.bin$");
    std::smatch m;
    if (!std::regex_match(nombre, m, patron))
        return fallar("The file name must be astrok_<motor>_v<x.y.z>_<yyyymmdd>.bin, as published.");
    std::string motorFichero = m[1].str();

    std::ifstream f(ruta, std::ios::binary);
    if (!f)
        return fallar("Cannot read " + ruta);
    std::vector<uint8_t> datos((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Desde 1.4.2: programa, web y 12 bytes al final (tamaños LE y "AKPK").
    std::vector<uint8_t> app = datos, web;
    if (datos.size() > 12 && std::memcmp(datos.data() + datos.size() - 4, "AKPK", 4) == 0)
    {
        size_t fin = datos.size() - 12;
        auto le32 = [&datos](size_t p)
        {
            return (size_t)datos[p] | ((size_t)datos[p + 1] << 8) | ((size_t)datos[p + 2] << 16) | ((size_t)datos[p + 3] << 24);
        };
        size_t tamApp = le32(fin), tamWeb = le32(fin + 4);
        if (tamApp + tamWeb != fin || tamWeb == 0 || tamWeb > TAMANO_MAXIMO_WEB)
            return fallar("Not a valid AstroK firmware package.");
        app.assign(datos.begin(), datos.begin() + tamApp);
        web.assign(datos.begin() + tamApp, datos.begin() + fin);
    }

    // Cabecera de imagen ESP32: 0xE9 y el chip en los bytes 12-13. La de otro
    // chip deja el equipo en un bucle de reinicios.
    if (app.size() < 24 || app[0] != 0xE9)
        return fallar("Not an ESP32 firmware image.");
    if (app.size() > TAMANO_MAXIMO_APP)
        return fallar("The image does not fit in the device.");
    int chipFichero = app[12] | (app[13] << 8);

    std::string hw;
    if (!equipo.consulta("DH", hw) || hw.find(';') == std::string::npos)
        return fallar("This device's firmware is too old to update over USB/Bluetooth.");
    std::string motorEquipo = hw.substr(0, hw.find(';'));
    int chipEquipo = std::atoi(hw.c_str() + hw.find(';') + 1);
    if (chipFichero != chipEquipo)
        return fallar("This firmware is for another chip: it would not boot.");
    if (strcasecmp(motorFichero.c_str(), motorEquipo.c_str()) != 0)
        return fallar("This firmware is for the " + motorFichero + " motor, and the device has the " +
                      motorEquipo + " one. Use astrok_" + motorEquipo + "_v..._.bin.");

    bool moviendo = false;
    if (equipo.moviendo(moviendo) && moviendo)
        return fallar("Stop the motor before updating.");

    if (hilo.joinable())
        hilo.join();
    grabando = true;
    FirmwareSP.setState(IPS_BUSY);
    FirmwareSP.apply();
    info("Updating to " + m[2].str() + "... Do not disconnect the device.");
    hilo = std::thread(&AstroKComun::hiloActualizacion, this, std::move(app), std::move(web));
}

void AstroKComun::hiloActualizacion(std::vector<uint8_t> app, std::vector<uint8_t> web)
{
    int ultimo = -1;
    auto progreso = [this, &ultimo](size_t hecho, size_t total)
    {
        int pct = (int)(100 * hecho / total);
        if (pct == ultimo)
            return;
        ultimo = pct;
        ProgresoNP[0].setValue(pct);
        ProgresoNP.setState(IPS_BUSY);
        ProgresoNP.apply();
    };

    std::string motivo;
    bool ok = equipo.actualizar(app, false, progreso, motivo);
    if (ok)
    {
        info("Firmware sent, the device is rebooting...");
        equipo.cerrar();
        ok = esperarReinicio();
        if (!ok)
            motivo = "the device did not come back after the update";
    }

    // La web va después y con el firmware nuevo ya arrancado, que es el que
    // entiende :UB...,W#. Si falla, el programa ya está actualizado.
    if (ok && !web.empty())
    {
        info("Sending the setup web page...");
        ultimo = -1;
        if (equipo.actualizar(web, true, progreso, motivo))
        {
            equipo.cerrar();
            if (!esperarReinicio())
                aviso("Web page sent, but the device did not come back yet.");
        }
        else
            aviso("The firmware was updated but the setup web page failed (" + motivo + "). Retry the update.");
    }

    grabando = false;
    ProgresoNP.setState(ok ? IPS_OK : IPS_ALERT);
    ProgresoNP.apply();
    FirmwareSP.setState(ok ? IPS_OK : IPS_ALERT);
    FirmwareSP.apply();
    if (ok)
    {
        InfoTP.apply();
        info("Firmware updated: " + std::string(InfoTP[FIRMWARE].getText()));
    }
    else
        error("Firmware update failed: " + motivo + ". The device keeps its previous firmware.");
    // Sin equipo abierto, el siguiente sondeo del driver lo da por perdido.
}

bool AstroKComun::esperarReinicio()
{
    dormir(3000);
    for (int i = 0; i < 20; i++)
    {
        std::string motivo;
        if (abrirCanal(motivo) && saludar(motivo))
            return true;
        equipo.cerrar();
        dormir(2000);
    }
    return false;
}
