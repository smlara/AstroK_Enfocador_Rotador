#pragma once

#include "astrok_canal.h"

#include <cstdint>
#include <functional>
#include <indijson.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// El protocolo serie del firmware (FocuserProtocol.cpp) sobre un Canal,
/// USB o BLE. Es el equivalente de DeviceLink en el driver ASCOM.
///
/// Todo va bajo un mutex: la actualización de firmware corre en su propio
/// hilo y ninguna consulta del bucle de INDI puede colarse entre un trozo y
/// su OK.
class Equipo
{
    public:
        /// Tope del recorrido del enfocador, igual que MAX_FOCUSER_STEPS.
        static constexpr long MaxPasosEnfocador = 999999;
        /// Cero lógico del rotador en el protocolo: :GP# da 32768 a 0 grados.
        static constexpr long CeroRotador = 32768;

        void usar(Canal *canal);
        void cerrar();
        bool abierto() const;
        bool esBle() const;

        /// Envía :cmd# y devuelve la respuesta sin el '#'.
        bool consulta(const std::string &cmd, std::string &respuesta, int timeoutMs = 3000);
        /// Comando sin respuesta (:SN, :SP, :FG, :FQ).
        bool enviar(const std::string &cmd);

        // ---- Posición y movimiento ----
        bool posicion(long &pasos);
        bool objetivo(long &pasos);
        bool moviendo(bool &si);
        bool mover(long pasos);
        bool sincronizar(long pasos);
        bool parar();

        // ---- Extensiones AstroK ----
        bool leerConfig(nlohmann::json &cfg);
        bool guardarConfig(const nlohmann::json &cfg);
        /// :CR#. El equipo contesta y se reinicia.
        bool valoresFabrica();
        /// :WR#. Los cambios de WiFi y de BLE solo se aplican al arrancar.
        void reiniciar();

        // ---- PIN de Bluetooth (ver BleLink.h) ----
        /// :BQ#: si el equipo tiene ya PIN.
        bool pinDefinido(bool &si);
        /// :BA<pin>#. Devuelve "OK", "ER:PIN", "ER:PARAMS" o "" si no contesta.
        std::string autenticar(const std::string &pin);

        // ---- Actualización de firmware (:UB# :UD# :UF# :UE# :UA# :US#) ----
        typedef std::function<void(size_t hecho, size_t total)> Progreso;
        /// Graba la imagen y el equipo se reinicia con ella. Con 'web' es la
        /// imagen LittleFS de la página de configuración. Al volver, el canal
        /// ya no vale.
        bool actualizar(const std::vector<uint8_t> &imagen, bool web, Progreso progreso, std::string &error);

        static long hex(const std::string &texto);
        static std::string base64(const std::string &datos);
        static bool desdeBase64(const std::string &texto, std::string &datos);

    private:
        bool pedirOk(const std::string &cmd, int timeoutMs, std::string &error);
        bool enviarTrozo(const std::string &cmd, size_t inicio, size_t n, size_t &offset, std::string &error);
        bool preguntarEscrito(size_t &escrito);

        std::unique_ptr<Canal> canal;
        mutable std::recursive_mutex mutex;
};
