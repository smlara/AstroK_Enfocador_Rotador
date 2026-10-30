#pragma once

#include "astrok_canal.h"

#include <string>
#include <vector>

typedef struct _GDBusConnection GDBusConnection;

/// Un equipo visto al buscar por Bluetooth.
struct EquipoBle
{
    std::string direccion;  // "7C:E8:B1:D3:8A:1A"
    std::string nombre;     // "AstroK_D38A1A"
    char modo;              // 'F' enfocador, 'R' rotador, '?' sin datos
    int rssi;
};

/// El protocolo por Bluetooth LE a través de BlueZ (D-Bus del sistema).
///
/// El firmware expone el Nordic UART Service: se escribe :XX# en RX y las
/// respuestas llegan como notificaciones de TX. No hay emparejamiento: el
/// acceso lo da el PIN de 4 cifras (:BA<pin>#), que gestiona quien abre.
///
/// Notificaciones y escritura van por los sockets que dan AcquireNotify y
/// AcquireWrite, así que leer es un read() con poll() como en USB y no hace
/// falta un bucle de GLib en el driver.
class CanalBle : public Canal
{
    public:
        ~CanalBle() override;

        /// Conecta con el equipo de esa dirección. Si BlueZ aún no lo conoce,
        /// lo busca unos segundos antes. nullptr y el motivo en 'error' si falla.
        static CanalBle *abrir(const std::string &direccion, std::string &error);

        /// Busca equipos AstroK durante unos segundos.
        static std::vector<EquipoBle> buscar(int segundos, std::string &error);

        bool escribir(const std::string &texto) override;
        bool esBle() const override
        {
            return true;
        }
        void cerrar() override;

    private:
        CanalBle() = default;
        bool conectar(const std::string &direccion, std::string &error);

        GDBusConnection *bus = nullptr;
        std::string rutaDispositivo;
        std::string rutaRx;
        // Socket de AcquireWrite; si BlueZ no lo da, se escribe con WriteValue.
        int fdEscritura = -1;
        size_t trozo = 20;
};
