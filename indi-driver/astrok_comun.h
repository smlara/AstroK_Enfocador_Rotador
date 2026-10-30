#pragma once

#include "astrok_equipo.h"

#include <atomic>
#include <defaultdevice.h>
#include <functional>
#include <thread>

/// Lo que comparten los drivers de enfocador y de rotador: la conexión (USB
/// o Bluetooth LE, con su PIN) y la configuración del equipo, que es la
/// misma para los dos salvo la sección del modo ("focuser" o "rotator").
///
/// El equipo es la fuente de verdad: todo lo que se cambia aquí se guarda en
/// su NVS con :CS#, igual que hacen la web y la app de Windows, y se relee de
/// él al conectar. El fichero de configuración de INDI solo guarda cómo
/// llegar al equipo.
class AstroKComun
{
    public:
        AstroKComun(INDI::DefaultDevice &dev, bool rotador);
        ~AstroKComun();

        Equipo equipo;
        /// La configuración del equipo tal como la dio :CG# la última vez.
        nlohmann::json cfg;

        /// La sección del modo dentro de cfg: "focuser" o "rotator".
        nlohmann::json &seccion();
        /// Cambia la sección con 'cambio', la guarda en el equipo y la relee.
        bool cambiarSeccion(const std::function<void(nlohmann::json &)> &cambio);

        /// Tras releer la configuración, para que el driver refresque lo suyo
        /// (máximo del enfocador, inversión, pasos por grado...).
        std::function<void()> alCambiarConfig;
        /// El equipo se ha reiniciado o se ha perdido el enlace: el driver
        /// tiene que pasar a desconectado.
        std::function<void(IPState)> alPerderEquipo;
        /// loadConfig y saveConfig son protegidos en DefaultDevice: el driver
        /// los presta.
        std::function<void(const char *)> cargarConfigIndi;
        std::function<void()> guardarConfigIndi;

        void initProperties();
        void ISGetProperties();
        void updateProperties(bool conectado);
        bool ISNewSwitch(const char *name, ISState *states, char *names[], int n);
        bool ISNewNumber(const char *name, double values[], char *names[], int n);
        bool ISNewText(const char *name, char *texts[], char *names[], int n);
        void saveConfigItems(FILE *fp);

        bool conectar();
        bool desconectar();

        /// Para el sondeo del driver: cuenta los fallos seguidos y, al tercero,
        /// intenta reconectar una vez. false si el equipo se ha perdido.
        bool comprobarEnlace(bool consultaOk);

        /// Hay una actualización de firmware en marcha: el sondeo se aparta.
        bool actualizando() const
        {
            return grabando;
        }

    private:
        void info(const std::string &texto);
        void aviso(const std::string &texto);
        void error(const std::string &texto);

        bool abrirCanal(std::string &motivo);
        bool saludar(std::string &motivo);
        std::string buscarUsb();
        void buscarBle();

        bool releerConfig();
        void rellenarPropiedades();
        bool guardarConfig(const nlohmann::json &nueva);
        bool guardarMotor(double values[], char *names[], int n);
        bool guardarWifi();
        void leerPinUsb();
        void leerDiagnostico();

        void empezarActualizacion();
        void hiloActualizacion(std::vector<uint8_t> app, std::vector<uint8_t> web);
        bool esperarReinicio();

        INDI::DefaultDevice &dev;
        bool rotador;
        int fallosSeguidos = 0;
        bool configCargada = false;
        long long confirmarFabricaHasta = 0;
        std::atomic<bool> grabando { false };
        std::thread hilo;

        // ---- Conexión (pestaña Connection) ----
        enum { USB, BLE };
        INDI::PropertySwitch TransporteSP {2};
        INDI::PropertyText PuertoTP {1};
        INDI::PropertySwitch BuscarUsbSP {1};
        enum { DIRECCION, PIN };
        INDI::PropertyText BleTP {2};
        INDI::PropertySwitch BuscarBleSP {1};
        INDI::PropertyText EncontradosTP {1};

        // ---- Equipo ----
        enum { FIRMWARE, HARDWARE, ENLACE, RED };
        INDI::PropertyText InfoTP {4};
        INDI::PropertyText DiagTP {1};
        INDI::PropertySwitch DiagSP {1};

        // ---- Motor ----
        INDI::PropertyNumber MotorNP {7};
        INDI::PropertyNumber PasosGradoNP {1};

        // ---- Inalámbrico ----
        enum { WIFI, BT };
        INDI::PropertySwitch RadiosSP {2};
        INDI::PropertyText ApTP {2};
        INDI::PropertyNumber WifiNP {2};
        static constexpr int REDES = 4;
        INDI::PropertyText RedesTP {REDES * 2};
        INDI::PropertyText PinUsbTP {1};

        // ---- Mantenimiento ----
        enum { REINICIAR, FABRICA };
        INDI::PropertySwitch AccionSP {2};
        INDI::PropertyText FirmwareTP {1};
        INDI::PropertySwitch FirmwareSP {1};
        INDI::PropertyNumber ProgresoNP {1};
};
