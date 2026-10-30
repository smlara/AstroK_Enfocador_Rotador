#include "astrok_ble.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <poll.h>
#include <thread>
#include <unistd.h>

namespace
{
// Nordic UART Service, igual que en BleLink.cpp. BlueZ da los UUID en minúsculas.
const char *NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char *NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

const char *IFACE_ADAPTADOR = "org.bluez.Adapter1";
const char *IFACE_DISPOSITIVO = "org.bluez.Device1";
const char *IFACE_CARACTERISTICA = "org.bluez.GattCharacteristic1";

void dormir(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string mensaje(GError *e)
{
    if (e == nullptr)
        return "unknown error";
    g_dbus_error_strip_remote_error(e);
    std::string m = e->message;
    g_error_free(e);
    return m;
}

std::string minusculas(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
    {
        return (char)std::tolower(c);
    });
    return s;
}

/// Lo que interesa de cada objeto que publica BlueZ.
struct ObjetoBluez
{
    std::string ruta;
    bool adaptador = false;
    bool dispositivo = false;
    bool caracteristica = false;
    std::string direccion;
    std::string nombre;
    std::string uuid;
    // Datos de fabricante 0xFFFF del anuncio, sin el identificador: "AKF" o "AKR".
    std::string fabricante;
    bool serviciosResueltos = false;
    bool conectado = false;
    bool tieneRssi = false;
    int rssi = 0;
};

void leerDispositivo(GVariant *props, ObjetoBluez &o)
{
    const char *s = nullptr;
    gboolean b = FALSE;
    gint16 rssi = 0;
    if (g_variant_lookup(props, "Address", "&s", &s))
        o.direccion = s;
    if (g_variant_lookup(props, "Name", "&s", &s))
        o.nombre = s;
    if (g_variant_lookup(props, "ServicesResolved", "b", &b))
        o.serviciosResueltos = b;
    if (g_variant_lookup(props, "Connected", "b", &b))
        o.conectado = b;
    if (g_variant_lookup(props, "RSSI", "n", &rssi))
    {
        o.tieneRssi = true;
        o.rssi = rssi;
    }

    // a{qv}: g_variant_lookup solo vale para claves de texto.
    GVariant *fab = g_variant_lookup_value(props, "ManufacturerData", G_VARIANT_TYPE("a{qv}"));
    if (fab == nullptr)
        return;
    GVariantIter it;
    g_variant_iter_init(&it, fab);
    guint16 id = 0;
    GVariant *valor = nullptr;
    while (g_variant_iter_next(&it, "{qv}", &id, &valor))
    {
        if (id == 0xFFFF && g_variant_is_of_type(valor, G_VARIANT_TYPE_BYTESTRING))
        {
            gsize n = 0;
            const char *d = (const char *)g_variant_get_fixed_array(valor, &n, 1);
            o.fabricante.assign(d, n);
        }
        g_variant_unref(valor);
    }
    g_variant_unref(fab);
}

std::vector<ObjetoBluez> objetos(GDBusConnection *bus, std::string &error)
{
    GError *e = nullptr;
    GVariant *r = g_dbus_connection_call_sync(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                  "GetManagedObjects", nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                  G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &e);
    if (r == nullptr)
    {
        error = "BlueZ not available (is bluetoothd running?): " + mensaje(e);
        return {};
    }

    std::vector<ObjetoBluez> lista;
    GVariant *dic = g_variant_get_child_value(r, 0);
    GVariantIter it;
    g_variant_iter_init(&it, dic);
    const char *ruta = nullptr;
    GVariant *ifaces = nullptr;
    while (g_variant_iter_next(&it, "{&o@a{sa{sv}}}", &ruta, &ifaces))
    {
        ObjetoBluez o;
        o.ruta = ruta;
        GVariantIter it2;
        g_variant_iter_init(&it2, ifaces);
        const char *iface = nullptr;
        GVariant *props = nullptr;
        while (g_variant_iter_next(&it2, "{&s@a{sv}}", &iface, &props))
        {
            std::string i = iface;
            if (i == IFACE_ADAPTADOR)
                o.adaptador = true;
            else if (i == IFACE_DISPOSITIVO)
            {
                o.dispositivo = true;
                leerDispositivo(props, o);
            }
            else if (i == IFACE_CARACTERISTICA)
            {
                o.caracteristica = true;
                const char *u = nullptr;
                if (g_variant_lookup(props, "UUID", "&s", &u))
                    o.uuid = minusculas(u);
            }
            g_variant_unref(props);
        }
        g_variant_unref(ifaces);
        lista.push_back(o);
    }
    g_variant_unref(dic);
    g_variant_unref(r);
    return lista;
}

/// Llamada sin respuesta útil. 'params' se consume (referencia flotante).
bool llamar(GDBusConnection *bus, const std::string &ruta, const char *iface, const char *metodo,
            GVariant *params, int timeoutMs, std::string &error)
{
    GError *e = nullptr;
    GVariant *r = g_dbus_connection_call_sync(bus, "org.bluez", ruta.c_str(), iface, metodo, params,
                  nullptr, G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr, &e);
    if (r == nullptr)
    {
        error = mensaje(e);
        return false;
    }
    g_variant_unref(r);
    return true;
}

std::string primerAdaptador(const std::vector<ObjetoBluez> &lista)
{
    for (const auto &o : lista)
        if (o.adaptador)
            return o.ruta;
    return "";
}

const ObjetoBluez *buscarDispositivo(const std::vector<ObjetoBluez> &lista, const std::string &direccion)
{
    std::string d = minusculas(direccion);
    for (const auto &o : lista)
        if (o.dispositivo && minusculas(o.direccion) == d)
            return &o;
    return nullptr;
}

// Solo dispositivos LE: el firmware no anuncia nada por BR/EDR.
void empezarBusqueda(GDBusConnection *bus, const std::string &adaptador)
{
    std::string ignorado;
    GVariantBuilder filtro;
    g_variant_builder_init(&filtro, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&filtro, "{sv}", "Transport", g_variant_new_string("le"));
    llamar(bus, adaptador, IFACE_ADAPTADOR, "SetDiscoveryFilter", g_variant_new("(a{sv})", &filtro), 5000, ignorado);
    // Falla con InProgress si ya está buscando: da igual, se aprovecha.
    llamar(bus, adaptador, IFACE_ADAPTADOR, "StartDiscovery", nullptr, 5000, ignorado);
}

void pararBusqueda(GDBusConnection *bus, const std::string &adaptador)
{
    std::string ignorado;
    llamar(bus, adaptador, IFACE_ADAPTADOR, "StopDiscovery", nullptr, 5000, ignorado);
}

GDBusConnection *busSistema(std::string &error)
{
    GError *e = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &e);
    if (bus == nullptr)
        error = "Cannot reach the system D-Bus: " + mensaje(e);
    return bus;
}

/// AcquireNotify / AcquireWrite: devuelve el socket y el MTU, o -1.
int adquirir(GDBusConnection *bus, const std::string &ruta, const char *metodo, uint16_t &mtu, std::string &error)
{
    GError *e = nullptr;
    GUnixFDList *fds = nullptr;
    GVariantBuilder opciones;
    g_variant_builder_init(&opciones, G_VARIANT_TYPE("a{sv}"));
    GVariant *r = g_dbus_connection_call_with_unix_fd_list_sync(bus, "org.bluez", ruta.c_str(),
                  IFACE_CARACTERISTICA, metodo, g_variant_new("(a{sv})", &opciones),
                  G_VARIANT_TYPE("(hq)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &fds, nullptr, &e);
    if (r == nullptr)
    {
        error = mensaje(e);
        return -1;
    }
    gint32 indice = 0;
    guint16 m = 0;
    g_variant_get(r, "(hq)", &indice, &m);
    g_variant_unref(r);
    int fd = -1;
    if (fds != nullptr)
    {
        fd = g_unix_fd_list_get(fds, indice, &e);
        if (fd < 0)
            error = mensaje(e);
        g_object_unref(fds);
    }
    mtu = m;
    return fd;
}
}

CanalBle::~CanalBle()
{
    CanalBle::cerrar();
}

std::vector<EquipoBle> CanalBle::buscar(int segundos, std::string &error)
{
    std::vector<EquipoBle> encontrados;
    GDBusConnection *bus = busSistema(error);
    if (bus == nullptr)
        return encontrados;

    std::string adaptador = primerAdaptador(objetos(bus, error));
    if (adaptador.empty())
    {
        if (error.empty())
            error = "No Bluetooth adapter found";
        g_object_unref(bus);
        return encontrados;
    }

    empezarBusqueda(bus, adaptador);
    dormir(segundos * 1000);
    // Se mira antes de parar: BlueZ quita el RSSI al dejar de buscar, y es lo
    // que distingue lo que está ahí ahora de lo que recuerda de otras veces.
    for (const auto &o : objetos(bus, error))
    {
        if (!o.dispositivo || o.ruta.rfind(adaptador + "/", 0) != 0)
            continue;
        bool esAstroK = o.fabricante.size() >= 3 && o.fabricante.compare(0, 2, "AK") == 0;
        if (!esAstroK && o.nombre.rfind("AstroK_", 0) != 0)
            continue;
        if (!o.tieneRssi && !o.conectado)
            continue;
        encontrados.push_back({ o.direccion, o.nombre.empty() ? o.direccion : o.nombre,
                                esAstroK ? o.fabricante[2] : '?', o.rssi });
    }
    pararBusqueda(bus, adaptador);
    g_object_unref(bus);

    std::sort(encontrados.begin(), encontrados.end(), [](const EquipoBle & a, const EquipoBle & b)
    {
        return a.rssi > b.rssi;
    });
    return encontrados;
}

CanalBle *CanalBle::abrir(const std::string &direccion, std::string &error)
{
    auto *c = new CanalBle();
    if (!c->conectar(direccion, error))
    {
        delete c;
        return nullptr;
    }
    return c;
}

bool CanalBle::conectar(const std::string &direccion, std::string &error)
{
    bus = busSistema(error);
    if (bus == nullptr)
        return false;

    auto lista = objetos(bus, error);
    std::string adaptador = primerAdaptador(lista);
    if (adaptador.empty())
    {
        if (error.empty())
            error = "No Bluetooth adapter found";
        return false;
    }

    // BlueZ solo conecta con lo que ya conoce: si el equipo no se ha visto
    // desde que arrancó bluetoothd, hay que buscarlo antes.
    const ObjetoBluez *disp = buscarDispositivo(lista, direccion);
    if (disp == nullptr)
    {
        empezarBusqueda(bus, adaptador);
        for (int i = 0; i < 40 && disp == nullptr; i++)
        {
            dormir(250);
            lista = objetos(bus, error);
            disp = buscarDispositivo(lista, direccion);
        }
        pararBusqueda(bus, adaptador);
        if (disp == nullptr)
        {
            error = "Bluetooth device " + direccion + " not found. Is it powered on and in range?";
            return false;
        }
    }
    rutaDispositivo = disp->ruta;

    // El primer intento falla a veces con le-connection-abort-by-local,
    // sobre todo justo después de otra conexión.
    bool conectado = false;
    for (int intento = 1; intento <= 3 && !conectado; intento++)
    {
        conectado = llamar(bus, rutaDispositivo, IFACE_DISPOSITIVO, "Connect", nullptr, 30000, error);
        if (!conectado)
            dormir(1000);
    }
    if (!conectado)
    {
        error = "Bluetooth connection failed: " + error;
        return false;
    }

    // Las características solo aparecen cuando BlueZ ha descubierto los servicios.
    std::string rutaTx;
    for (int i = 0; i < 75 && (rutaRx.empty() || rutaTx.empty()); i++)
    {
        lista = objetos(bus, error);
        const ObjetoBluez *d = buscarDispositivo(lista, direccion);
        if (d != nullptr && d->serviciosResueltos)
        {
            for (const auto &o : lista)
            {
                if (!o.caracteristica || o.ruta.rfind(rutaDispositivo + "/", 0) != 0)
                    continue;
                if (o.uuid == NUS_RX)
                    rutaRx = o.ruta;
                else if (o.uuid == NUS_TX)
                    rutaTx = o.ruta;
            }
            if (rutaRx.empty() || rutaTx.empty())
            {
                error = "The device does not offer the AstroK serial service";
                return false;
            }
        }
        else
            dormir(200);
    }
    if (rutaRx.empty() || rutaTx.empty())
    {
        error = "Timed out discovering the device's Bluetooth services";
        return false;
    }

    uint16_t mtu = 0;
    fdLectura = adquirir(bus, rutaTx, "AcquireNotify", mtu, error);
    if (fdLectura < 0)
    {
        // "Notify acquired": otro programa tiene el equipo abierto.
        error = "Cannot receive from the device (" + error + "). Is another program connected to it?";
        return false;
    }

    std::string sinSocket;
    fdEscritura = adquirir(bus, rutaRx, "AcquireWrite", mtu, sinSocket);
    // Lo mismo que el firmware: MTU - 3, y como mucho 244.
    trozo = (mtu > 23) ? std::min<size_t>(mtu - 3, 244) : 20;
    return true;
}

bool CanalBle::escribir(const std::string &texto)
{
    if (bus == nullptr || caido)
        return false;

    for (size_t hecho = 0; hecho < texto.size(); )
    {
        size_t n = std::min(trozo, texto.size() - hecho);
        const char *p = texto.data() + hecho;

        if (fdEscritura >= 0)
        {
            // Cada write() es un paquete ATT (socket SOCK_SEQPACKET).
            ssize_t r = ::write(fdEscritura, p, n);
            if (r < 0 && (errno == EAGAIN || errno == EINTR))
            {
                pollfd pfd { fdEscritura, POLLOUT, 0 };
                if (::poll(&pfd, 1, 3000) <= 0)
                    return false;
                continue;
            }
            if (r != (ssize_t)n)
            {
                caido = true;
                return false;
            }
        }
        else
        {
            GVariantBuilder opciones;
            g_variant_builder_init(&opciones, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&opciones, "{sv}", "type", g_variant_new_string("command"));
            GVariant *datos = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, p, n, 1);
            std::string error;
            if (!llamar(bus, rutaRx, IFACE_CARACTERISTICA, "WriteValue",
                        g_variant_new("(@aya{sv})", datos, &opciones), 5000, error))
            {
                caido = true;
                return false;
            }
        }
        hecho += n;
    }
    return true;
}

void CanalBle::cerrar()
{
    if (fdEscritura >= 0)
        ::close(fdEscritura);
    fdEscritura = -1;
    // Cerrar el socket de notificaciones es lo que las desactiva en BlueZ.
    Canal::cerrar();

    if (bus != nullptr)
    {
        if (!rutaDispositivo.empty())
        {
            std::string ignorado;
            llamar(bus, rutaDispositivo, IFACE_DISPOSITIVO, "Disconnect", nullptr, 5000, ignorado);
        }
        g_object_unref(bus);
        bus = nullptr;
    }
    rutaDispositivo.clear();
    rutaRx.clear();
}
