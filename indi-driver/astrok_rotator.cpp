#include "astrok_rotator.h"

#include <cmath>
#include <cstring>
#include <memory>

static std::unique_ptr<AstroKRotator> rotador(new AstroKRotator());

static double normalizar(double grados)
{
    grados = std::fmod(grados, 360.0);
    return grados < 0 ? grados + 360.0 : grados;
}

AstroKRotator::AstroKRotator() : comun(*this, true)
{
    setVersion(1, 0);
    // La conexión la lleva AstroKComun: USB abierto a mano (el conector serie
    // de INDI resetea el C3 al conectar) o BLE.
    setRotatorConnection(CONNECTION_NONE);
    RI::SetCapability(ROTATOR_CAN_ABORT | ROTATOR_CAN_SYNC | ROTATOR_CAN_REVERSE);

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

const char *AstroKRotator::getDefaultName()
{
    return "AstroK Rotator";
}

bool AstroKRotator::initProperties()
{
    INDI::Rotator::initProperties();
    comun.initProperties();
    addAuxControls();
    setDefaultPollingPeriod(500);
    return true;
}

void AstroKRotator::ISGetProperties(const char *dev)
{
    INDI::Rotator::ISGetProperties(dev);
    comun.ISGetProperties();
}

bool AstroKRotator::updateProperties()
{
    INDI::Rotator::updateProperties();
    comun.updateProperties(isConnected());
    return true;
}

bool AstroKRotator::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()) && comun.ISNewNumber(name, values, names, n))
        return true;
    return INDI::Rotator::ISNewNumber(dev, name, values, names, n);
}

bool AstroKRotator::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()) && comun.ISNewSwitch(name, states, names, n))
        return true;
    return INDI::Rotator::ISNewSwitch(dev, name, states, names, n);
}

bool AstroKRotator::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()) && comun.ISNewText(name, texts, names, n))
        return true;
    return INDI::Rotator::ISNewText(dev, name, texts, names, n);
}

bool AstroKRotator::Connect()
{
    if (!comun.conectar())
        return false;
    SetTimer(getCurrentPollingPeriod());
    return true;
}

bool AstroKRotator::Disconnect()
{
    return comun.desconectar();
}

void AstroKRotator::perderEquipo(IPState estado)
{
    comun.equipo.cerrar();
    setConnected(false, estado);
    updateProperties();
}

double AstroKRotator::pasosAGrados(long pasos) const
{
    return normalizar((pasos - Equipo::CeroRotador) / pasosPorGrado);
}

long AstroKRotator::gradosAPasos(double grados) const
{
    return Equipo::CeroRotador + std::lround(normalizar(grados) * pasosPorGrado);
}

/// Pasos por grado y sentido salen del equipo, no del fichero de INDI.
void AstroKRotator::aplicarConfig()
{
    const nlohmann::json &s = comun.seccion();
    if (s.contains("steps_per_degree") && s["steps_per_degree"].is_number() && s["steps_per_degree"].get<double>() > 0)
        pasosPorGrado = s["steps_per_degree"].get<double>();
    bool invertido = s.contains("reverse") && s["reverse"].is_boolean() && s["reverse"].get<bool>();

    ReverseRotatorSP[INDI_ENABLED].setState(invertido ? ISS_ON : ISS_OFF);
    ReverseRotatorSP[INDI_DISABLED].setState(invertido ? ISS_OFF : ISS_ON);
    if (isConnected())
        ReverseRotatorSP.apply();
}

void AstroKRotator::TimerHit()
{
    if (!isConnected())
        return;

    if (comun.actualizando())
    {
        SetTimer(getCurrentPollingPeriod());
        return;
    }
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
        double grados = pasosAGrados(pos);
        IPState estado = mueve ? IPS_BUSY : IPS_OK;
        bool termino = !mueve && GotoRotatorNP.getState() == IPS_BUSY;
        if (std::abs(grados - GotoRotatorNP[0].getValue()) > 0.005 || estado != GotoRotatorNP.getState())
        {
            GotoRotatorNP[0].setValue(grados);
            GotoRotatorNP.setState(estado);
            GotoRotatorNP.apply();
        }
        if (termino)
            LOGF_INFO("Rotator at %.2f°.", grados);
    }

    SetTimer(getCurrentPollingPeriod());
}

IPState AstroKRotator::MoveRotator(double angle)
{
    // El firmware ya elige el camino más corto al recibir :SN en modo rotador.
    return comun.equipo.mover(gradosAPasos(angle)) ? IPS_BUSY : IPS_ALERT;
}

bool AstroKRotator::SyncRotator(double angle)
{
    return comun.equipo.sincronizar(gradosAPasos(angle));
}

bool AstroKRotator::AbortRotator()
{
    return comun.equipo.parar();
}

bool AstroKRotator::ReverseRotator(bool enabled)
{
    return comun.cambiarSeccion([enabled](nlohmann::json & s)
    {
        s["reverse"] = enabled;
    });
}

bool AstroKRotator::saveConfigItems(FILE *fp)
{
    // La inversión no: es del equipo (rotator.reverse). Ver el enfocador.
    INDI::DefaultDevice::saveConfigItems(fp);
    PresetNP.save(fp);
    RotatorLimitsNP.save(fp);
    comun.saveConfigItems(fp);
    return true;
}
