#pragma once

#include <string>

/// Por donde viaja el protocolo serie del firmware (:XX#): el USB del C3 o
/// Bluetooth LE. Es lo mismo que ICanal en el driver ASCOM.
class Canal
{
    public:
        virtual ~Canal();

        virtual bool escribir(const std::string &texto) = 0;
        virtual bool esBle() const = 0;

        /// Lee hasta 'fin' (que no se devuelve). false si se agota el tiempo
        /// o el canal se ha caído.
        bool leerHasta(char fin, int timeoutMs, std::string &salida);

        /// Descarta lo recibido y no leído: restos de una respuesta que llegó
        /// tarde no deben tomarse por la respuesta del comando siguiente.
        void vaciar();

        virtual void cerrar();

    protected:
        // Por aquí llegan los datos: el propio puerto en USB, el socket de
        // AcquireNotify en BLE.
        int fdLectura = -1;
        std::string resto;
        bool caido = false;
};

class CanalSerie : public Canal
{
    public:
        /// Abre el puerto. Devuelve nullptr y deja el motivo en 'error'.
        static CanalSerie *abrir(const std::string &puerto, std::string &error);

        bool escribir(const std::string &texto) override;
        bool esBle() const override
        {
            return false;
        }
};
