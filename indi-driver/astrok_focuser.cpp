#include "astrok_focuser.h"

#include <cmath>
#include <cstring>
#include <memory>

static std::unique_ptr<AstroKFocuser> enfocador(new AstroKFocuser());

AstroKFocuser::AstroKFocuser() : comun(*this, false)
{
    setVersion(ASTROK_VERSION_MAYOR, ASTROK_VERSION_MENOR);
    // La conexión la lleva AstroKComun: USB abierto a mano (el conector serie
    // de INDI resetea el C3 al conectar) o BLE.
    setSupportedConnections(CONNECTION_NONE);
    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_ABORT |
                      FOCUSER_CAN_REVERSE | FOCUSER_CAN_SYNC);

    comun.alCambiarConfig = [this]()
    {
        aplicarConfig();
    };
    comun.alPerderEquipo = [this](IPState estado)
    {
        perderEquipo(estado);
    };
    comun.cargarConfigIndi = [this](const char *propiedad)
    {
        loadConfig(true, propiedad);
    };
    comun.guardarConfigIndi = [this]()
    {
        saveConfig(true);
    };
}

const char *AstroKFocuser::getDefaultName()
{
    return "AstroK Focuser";
}

bool AstroKFocuser::initProperties()
{
    INDI::Focuser::initProperties();
    comun.initProperties();

    FocusMaxPosNP[0].setMax(Equipo::MaxPasosEnfocador);
    FocusAbsPosNP[0].setMax(Equipo::MaxPasosEnfocador);
    FocusSyncNP[0].setMax(Equipo::MaxPasosEnfocador);
    FocusRelPosNP[0].setMax(Equipo::MaxPasosEnfocador / 2);

    addAuxControls();
    setDefaultPollingPeriod(500);
    return true;
}

void AstroKFocuser::ISGetProperties(const char *dev)
{
    INDI::Focuser::ISGetProperties(dev);
    comun.ISGetProperties();
}

bool AstroKFocuser::updateProperties()
{
    INDI::Focuser::updateProperties();
    comun.updateProperties(isConnected());
    return true;
}

bool AstroKFocuser::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()) && comun.ISNewNumber(name, values, names, n))
        return true;
    return INDI::Focuser::ISNewNumber(dev, name, values, names, n);
}

bool AstroKFocuser::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()) && comun.ISNewSwitch(name, states, names, n))
        return true;
    return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
}

bool AstroKFocuser::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()) && comun.ISNewText(name, texts, names, n))
        return true;
    return INDI::Focuser::ISNewText(dev, name, texts, names, n);
}

bool AstroKFocuser::Connect()
{
    if (!comun.conectar())
        return false;
    SetTimer(getCurrentPollingPeriod());
    return true;
}

bool AstroKFocuser::Disconnect()
{
    return comun.desconectar();
}

void AstroKFocuser::perderEquipo(IPState estado)
{
    comun.equipo.cerrar();
    setConnected(false, estado);
    updateProperties();
}

/// Tope y sentido salen del equipo, no del fichero de INDI.
void AstroKFocuser::aplicarConfig()
{
    const nlohmann::json &s = comun.seccion();
    double tope = Equipo::MaxPasosEnfocador;
    if (s.contains("max_steps") && s["max_steps"].is_number())
        tope = std::min<double>(s["max_steps"].get<double>(), Equipo::MaxPasosEnfocador);
    bool invertido = s.contains("reverse") && s["reverse"].is_boolean() && s["reverse"].get<bool>();

    FocusMaxPosNP[0].setValue(tope);
    FocusAbsPosNP[0].setMax(tope);
    FocusSyncNP[0].setMax(tope);
    FocusAbsPosNP[0].setStep(std::max(1.0, tope / 50.0));
    FocusSyncNP[0].setStep(std::max(1.0, tope / 50.0));
    FocusRelPosNP[0].setMax(tope / 2);
    FocusRelPosNP[0].setStep(std::max(1.0, tope / 100.0));
    FocusReverseSP[INDI_ENABLED].setState(invertido ? ISS_ON : ISS_OFF);
    FocusReverseSP[INDI_DISABLED].setState(invertido ? ISS_OFF : ISS_ON);

    if (isConnected())
    {
        FocusMaxPosNP.apply();
        FocusAbsPosNP.updateMinMax();
        FocusSyncNP.updateMinMax();
        FocusRelPosNP.updateMinMax();
        FocusReverseSP.apply();
    }
}

void AstroKFocuser::TimerHit()
{
    if (!isConnected())
        return;

    if (comun.actualizando())
    {
        SetTimer(getCurrentPollingPeriod());
        return;
    }
    // Una actualización que no ha conseguido volver a conectar deja el
    // equipo cerrado.
    if (!comun.equipo.abierto())
    {
        perderEquipo(IPS_ALERT);
        return;
    }

    long pos = 0;
    bool mueve = false;
    bool ok = comun.equipo.posicion(pos) && comun.equipo.moviendo(mueve);
    if (!comun.comprobarEnlace(ok))
    {
        perderEquipo(IPS_ALERT);
        return;
    }

    if (ok)
    {
        IPState estado = mueve ? IPS_BUSY : IPS_OK;
        bool termino = !mueve && FocusAbsPosNP.getState() == IPS_BUSY;
        if (pos != (long)FocusAbsPosNP[0].getValue() || estado != FocusAbsPosNP.getState())
        {
            FocusAbsPosNP[0].setValue(pos);
            FocusAbsPosNP.setState(estado);
            FocusAbsPosNP.apply();
        }
        if (termino)
        {
            if (FocusRelPosNP.getState() == IPS_BUSY)
            {
                FocusRelPosNP.setState(IPS_OK);
                FocusRelPosNP.apply();
            }
            LOGF_INFO("Focuser at %ld.", pos);
        }
    }

    SetTimer(getCurrentPollingPeriod());
}

IPState AstroKFocuser::MoveAbsFocuser(uint32_t targetTicks)
{
    if (targetTicks > FocusMaxPosNP[0].getValue())
    {
        LOGF_ERROR("Position %u is beyond the maximum (%.0f).", targetTicks, FocusMaxPosNP[0].getValue());
        return IPS_ALERT;
    }
    return comun.equipo.mover(targetTicks) ? IPS_BUSY : IPS_ALERT;
}

IPState AstroKFocuser::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    double actual = FocusAbsPosNP[0].getValue();
    double destino = (dir == FOCUS_INWARD) ? actual - ticks : actual + ticks;
    destino = std::max(0.0, std::min(destino, FocusMaxPosNP[0].getValue()));

    if (!comun.equipo.mover(std::lround(destino)))
        return IPS_ALERT;
    FocusAbsPosNP.setState(IPS_BUSY);
    FocusAbsPosNP.apply();
    return IPS_BUSY;
}

bool AstroKFocuser::AbortFocuser()
{
    return comun.equipo.parar();
}

bool AstroKFocuser::SyncFocuser(uint32_t ticks)
{
    return comun.equipo.sincronizar(ticks);
}

bool AstroKFocuser::ReverseFocuser(bool enabled)
{
    return comun.cambiarSeccion([enabled](nlohmann::json & s)
    {
        s["reverse"] = enabled;
    });
}

bool AstroKFocuser::SetFocuserMaxPosition(uint32_t ticks)
{
    if (ticks < 1 || ticks > Equipo::MaxPasosEnfocador)
    {
        LOGF_ERROR("The maximum must be between 1 and %ld.", Equipo::MaxPasosEnfocador);
        return false;
    }
    return comun.cambiarSeccion([ticks](nlohmann::json & s)
    {
        s["max_steps"] = (long)ticks;
    });
}

bool AstroKFocuser::saveConfigItems(FILE *fp)
{
    // Ni el máximo ni la inversión: son del equipo. Si se guardaran aquí,
    // un cliente que cargue la configuración al conectar los pisaría con lo
    // que hubiera la última vez en este ordenador.
    INDI::DefaultDevice::saveConfigItems(fp);
    PresetNP.save(fp);
    comun.saveConfigItems(fp);
    return true;
}
