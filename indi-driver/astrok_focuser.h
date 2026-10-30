#pragma once

#include "astrok_comun.h"

#include <indifocuser.h>

/// Driver INDI del enfocador AstroK, por USB o Bluetooth LE.
///
/// El enfocador es absoluto: 0 a max_steps, con el tope guardado en el
/// propio equipo (focuser.max_steps), como la inversión (focuser.reverse).
class AstroKFocuser : public INDI::Focuser
{
    public:
        AstroKFocuser();

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

        IPState MoveAbsFocuser(uint32_t targetTicks) override;
        IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
        bool AbortFocuser() override;
        bool SyncFocuser(uint32_t ticks) override;
        bool ReverseFocuser(bool enabled) override;
        bool SetFocuserMaxPosition(uint32_t ticks) override;

        bool saveConfigItems(FILE *fp) override;

    private:
        void aplicarConfig();
        void perderEquipo(IPState estado);

        AstroKComun comun;
};
