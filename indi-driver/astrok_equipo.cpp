#include "astrok_equipo.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glib.h>

void Equipo::usar(Canal *nuevo)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    canal.reset(nuevo);
}

void Equipo::cerrar()
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    canal.reset();
}

bool Equipo::abierto() const
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    return canal != nullptr;
}

bool Equipo::esBle() const
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    return canal != nullptr && canal->esBle();
}

bool Equipo::consulta(const std::string &cmd, std::string &respuesta, int timeoutMs)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (canal == nullptr)
        return false;
    canal->vaciar();
    if (!canal->escribir(":" + cmd + "#"))
        return false;
    if (!canal->leerHasta('#', timeoutMs, respuesta))
        return false;
    // Por USB, un reinicio puede dejar delante algo del banner de arranque.
    size_t fin = respuesta.find_last_not_of(" \r\n\t");
    respuesta.erase(fin == std::string::npos ? 0 : fin + 1);
    size_t ini = respuesta.find_last_of("\r\n");
    if (ini != std::string::npos)
        respuesta.erase(0, ini + 1);
    return true;
}

bool Equipo::enviar(const std::string &cmd)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    return canal != nullptr && canal->escribir(":" + cmd + "#");
}

long Equipo::hex(const std::string &texto)
{
    // Un negativo llega en complemento a 2 de 32 bits (8 cifras).
    return (long)(int32_t)(uint32_t)std::strtoul(texto.c_str(), nullptr, 16);
}

static std::string comoHex(long valor)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04lX", (unsigned long)(uint32_t)valor);
    return buf;
}

bool Equipo::posicion(long &pasos)
{
    std::string r;
    if (!consulta("GP", r) || r.empty())
        return false;
    pasos = hex(r);
    return true;
}

bool Equipo::objetivo(long &pasos)
{
    std::string r;
    if (!consulta("GN", r) || r.empty())
        return false;
    pasos = hex(r);
    return true;
}

bool Equipo::moviendo(bool &si)
{
    std::string r;
    if (!consulta("GI", r) || r.empty())
        return false;
    si = (r != "00");
    return true;
}

bool Equipo::mover(long pasos)
{
    std::lock_guard<std::recursive_mutex> l(mutex);
    return enviar("SN" + comoHex(pasos)) && enviar("FG");
}

bool Equipo::sincronizar(long pasos)
{
    return enviar("SP" + comoHex(pasos));
}

bool Equipo::parar()
{
    return enviar("FQ");
}

// ---- Configuración -----------------------------------------------------
// El JSON viaja en base64 porque SSID y contraseñas pueden llevar ':' o '#'.

static const char *ALFABETO = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Equipo::base64(const std::string &datos)
{
    std::string s;
    s.reserve((datos.size() + 2) / 3 * 4);
    for (size_t i = 0; i < datos.size(); i += 3)
    {
        uint32_t v = (uint8_t)datos[i] << 16;
        if (i + 1 < datos.size())
            v |= (uint8_t)datos[i + 1] << 8;
        if (i + 2 < datos.size())
            v |= (uint8_t)datos[i + 2];
        s += ALFABETO[(v >> 18) & 63];
        s += ALFABETO[(v >> 12) & 63];
        s += (i + 1 < datos.size()) ? ALFABETO[(v >> 6) & 63] : '=';
        s += (i + 2 < datos.size()) ? ALFABETO[v & 63] : '=';
    }
    return s;
}

bool Equipo::desdeBase64(const std::string &texto, std::string &datos)
{
    datos.clear();
    uint32_t v = 0;
    int bits = 0;
    for (char c : texto)
    {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ')
            continue;
        const char *p = std::strchr(ALFABETO, c);
        if (p == nullptr || c == '\0')
            return false;
        v = (v << 6) | (uint32_t)(p - ALFABETO);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            datos += (char)((v >> bits) & 0xFF);
        }
    }
    return true;
}

bool Equipo::leerConfig(nlohmann::json &cfg)
{
    std::string r, json;
    if (!consulta("CG", r) || !desdeBase64(r, json))
        return false;
    try
    {
        cfg = nlohmann::json::parse(json);
        return cfg.is_object();
    }
    catch (...)
    {
        return false;
    }
}

bool Equipo::guardarConfig(const nlohmann::json &cfg)
{
    std::string r;
    return consulta("CS" + base64(cfg.dump()), r) && r == "OK";
}

bool Equipo::valoresFabrica()
{
    // Contesta OK# justo antes de reiniciar.
    std::string r;
    return consulta("CR", r) && r == "OK";
}

void Equipo::reiniciar()
{
    // Contesta OK# y se reinicia; que no llegue es normal.
    std::string r;
    consulta("WR", r, 1000);
}

bool Equipo::pinDefinido(bool &si)
{
    std::string r;
    if (!consulta("BQ", r) || (r != "0" && r != "1"))
        return false;
    si = (r == "1");
    return true;
}

std::string Equipo::autenticar(const std::string &pin)
{
    std::string r;
    return consulta("BA" + pin, r) ? r : "";
}

// ---- Actualización de firmware -----------------------------------------
// Mismo procedimiento que DeviceLink.Actualizar en el driver ASCOM: se manda
// un trozo y no se manda el siguiente hasta tener su OK; si no llega, :US#
// dice cuánto hay escrito de verdad y se sigue desde ahí.

static const size_t TROZO_FIRMWARE = 1024;
static const int REINTENTOS_POR_TROZO = 5;
// Lo más que acepta :UF# de una vez.
static const size_t VACIO_MAXIMO = 0x10000;

static size_t contarVacios(const std::vector<uint8_t> &imagen, size_t inicio, size_t maximo)
{
    size_t fin = std::min(imagen.size(), inicio + maximo);
    size_t i = inicio;
    while (i < fin && imagen[i] == 0xFF)
        i++;
    return i - inicio;
}

static std::string enHex(size_t v)
{
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%zX", v);
    return buf;
}

bool Equipo::pedirOk(const std::string &cmd, int timeoutMs, std::string &error)
{
    std::string r;
    if (!consulta(cmd, r, timeoutMs))
    {
        error = "no answer to :" + cmd.substr(0, 2) + "#";
        return false;
    }
    if (r != "OK")
    {
        error = "rejected by the device: " + (r.rfind("ER:", 0) == 0 ? r.substr(3) : r);
        return false;
    }
    return true;
}

bool Equipo::preguntarEscrito(size_t &escrito)
{
    std::string r;
    if (!consulta("US", r, 3000) || r.empty() || r.rfind("ER", 0) == 0)
        return false;
    escrito = (size_t)std::strtoul(r.c_str(), nullptr, 16);
    return true;
}

bool Equipo::enviarTrozo(const std::string &cmd, size_t inicio, size_t n, size_t &offset, std::string &error)
{
    for (int intento = 1; ; intento++)
    {
        std::string r;
        bool contesta = consulta(cmd, r, 3000);
        if (contesta && r == "OK")
        {
            offset = inicio + n;
            return true;
        }
        if (contesta && r.rfind("ER:", 0) == 0 && r != "ER:BASE64")
        {
            error = "rejected by the device: " + r.substr(3);
            return false;
        }
        if (intento >= REINTENTOS_POR_TROZO)
        {
            error = "no answer from the device at offset 0x" + enHex(inicio);
            return false;
        }

        // Sin respuesta (o trozo corrupto): ¿dónde está de verdad?
        size_t escrito = 0;
        if (preguntarEscrito(escrito))
        {
            if (escrito > inicio)
            {
                offset = escrito;   // sí llegó, se perdió el OK
                return true;
            }
            if (escrito < inicio)
            {
                error = "device is behind: 0x" + enHex(escrito);
                return false;
            }
        }
    }
}

bool Equipo::actualizar(const std::vector<uint8_t> &imagen, bool web, Progreso progreso, std::string &error)
{
    gchar *md5 = g_compute_checksum_for_data(G_CHECKSUM_MD5, imagen.data(), imagen.size());
    std::string cmdInicio = "UB" + std::to_string(imagen.size()) + "," + md5 + (web ? ",W" : "");
    g_free(md5);

    std::lock_guard<std::recursive_mutex> l(mutex);
    if (canal == nullptr)
    {
        error = "not connected";
        return false;
    }

    bool ok = pedirOk(cmdInicio, 3000, error);
    size_t offset = 0;
    while (ok && offset < imagen.size())
    {
        // La web es casi toda flash vacía: los tramos a 0xFF van con :UF#
        // sin mandar los bytes. Solo para la web: el firmware que se
        // actualiza con el programa puede ser anterior a :UF#.
        size_t vacios = web ? contarVacios(imagen, offset, VACIO_MAXIMO) : 0;
        if (vacios >= TROZO_FIRMWARE)
        {
            vacios -= vacios % TROZO_FIRMWARE;
            ok = enviarTrozo("UF" + enHex(offset) + "," + enHex(vacios), offset, vacios, offset, error);
        }
        else
        {
            size_t n = std::min(TROZO_FIRMWARE, imagen.size() - offset);
            std::string trozo((const char *)imagen.data() + offset, n);
            ok = enviarTrozo("UD" + enHex(offset) + "," + base64(trozo), offset, n, offset, error);
        }
        if (ok && progreso)
            progreso(offset, imagen.size());
    }

    // :UE# comprueba el MD5 de toda la imagen antes de contestar.
    if (ok)
        ok = pedirOk("UE", 15000, error);
    if (!ok)
    {
        std::string ignorada;
        consulta("UA", ignorada, 2000);
    }
    return ok;
}
