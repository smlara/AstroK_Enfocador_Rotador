#pragma once

#include "astrok_comun.h"

#include <indirotator.h>

/// Driver INDI del rotador AstroK, por USB o Bluetooth LE.
///
/// El protocolo lleva el rotador en pasos, con 32768 como el cero, e INDI lo
/// quiere en grados: se convierte con el steps_per_degree del propio equipo,
/// que se relee al conectar y cada vez que cambia la configuración.
class AstroKRotator : public INDI::Rotator
{
    public:
        AstroKRotator();

        const char *getDefaultName() override;
        bool initProperties() override;
        void ISGetProperties(const char *dev) override;
        bool updateProperties() override;
        bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
        bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
        bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;

    protected:
        bool Connect() override;
        bool Disconnect() override;
        void TimerHit() override;

        IPState MoveRotator(double angle) override;
        bool SyncRotator(double angle) override;
        bool AbortRotator() override;
        bool ReverseRotator(bool enabled) override;

        bool saveConfigItems(FILE *fp) override;

    private:
        void aplicarConfig();
        void perderEquipo(IPState estado);
        double pasosAGrados(long pasos) const;
        long gradosAPasos(double grados) const;

        AstroKComun comun;
        double pasosPorGrado = 37.75;
};
