using System;
using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;
using ASCOM;
using ASCOM.DeviceInterface;
using ASCOM.Utilities;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Driver ASCOM de rotador para AstroK sobre el mismo protocolo serie
    /// compatible Moonlite que el enfocador.
    ///
    /// Enfocador y rotador son productos distintos (mecánica y forma propias)
    /// aunque compartan electrónica y firmware, así que cada uno tiene su
    /// driver, su ProgID y su instalador. El equipo decide su modo con un
    /// jumper al arrancar y lo reporta con :DM#; si no coincide, aquí se
    /// rechaza la conexión en vez de funcionar mal en silencio.
    ///
    /// El protocolo transporta el rotador en PASOS, con 32768 como el cero
    /// lógico, y ASCOM lo quiere en GRADOS: de ahí las conversiones, que usan
    /// el steps_per_degree configurado en el propio equipo.
    /// </summary>
    [Guid("b7e41d38-5a92-4c60-8e15-7d2f9a4c1e63")]
    [ClassInterface(ClassInterfaceType.None)]
    [ComVisible(true)]
    public class Rotator : IRotatorV3
    {
        internal const string DriverId = "ASCOM.AstroK.Rotator";
        private const string DriverDescriptionText = "AstroK Rotator";

        private const string ComPortProfileName = "COM Port";
        private const string ComPortDefault = "COM5";

        // Cero lógico del rotador en el protocolo Moonlite (ver FocuserProtocol).
        private const int CeroProtocolo = 32768;

        private readonly DeviceLink _link = new DeviceLink();
        private bool _connected;
        private string _comPort;
        private double _pasosPorGrado = 37.75; // se relee del equipo al conectar
        private TraceLogger _log;

        public Rotator()
        {
            _log = new TraceLogger("", "AstroKRotator");
            ReadProfile();
            _log.Enabled = true;
            _log.LogMessage("Rotator", "Driver started");
        }

        #region Conversiones pasos <-> grados

        private static double Normalizar(double grados)
        {
            grados = grados % 360.0;
            if (grados < 0) grados += 360.0;
            return grados;
        }

        private double PasosAGrados(int pasos)
        {
            return Normalizar((pasos - CeroProtocolo) / _pasosPorGrado);
        }

        private int GradosAPasos(double grados)
        {
            return CeroProtocolo + (int)Math.Round(Normalizar(grados) * _pasosPorGrado);
        }

        /// <summary>
        /// Lee del equipo los pasos por grado. Es la fuente de verdad: si se
        /// usara un valor fijo, cualquier cambio de reductora dejaría los
        /// ángulos mal sin que nada lo indicara.
        /// </summary>
        private void LeerPasosPorGradoDelEquipo()
        {
            try
            {
                string json = _link.GetConfigJson();
                var raiz = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);

                object rot;
                if (raiz != null && raiz.TryGetValue("rotator", out rot) && rot is Dictionary<string, object>)
                {
                    object spd;
                    if (((Dictionary<string, object>)rot).TryGetValue("steps_per_degree", out spd) && spd != null)
                    {
                        double valor = Convert.ToDouble(spd);
                        if (valor > 0)
                        {
                            _pasosPorGrado = valor;
                            _log.LogMessage("Connected", "Device steps_per_degree: " + _pasosPorGrado);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                _log.LogMessage("Connected", "Could not read device config: " + ex.Message);
            }
        }

        #endregion

        #region IRotatorV3

        public bool Connected
        {
            get { return _connected; }
            set
            {
                if (value == _connected) return;

                if (value)
                {
                    _link.Open(_comPort);
                    try
                    {
                        string version = _link.GetVersion();
                        if (string.IsNullOrEmpty(version))
                            throw new DriverException("Device does not respond to :GV#");

                        // El jumper decide el modo. Conectar el driver de
                        // rotador a un enfocador daría posiciones sin sentido,
                        // así que se comprueba y se avisa.
                        string modo = _link.GetDeviceMode();
                        if (modo == "FOCUSER")
                            throw new DriverException(
                                "El equipo ha arrancado como ENFOCADOR, no como rotador. " +
                                "Revisa el jumper de modo, o usa el driver AstroK Focuser.");

                        LeerPasosPorGradoDelEquipo();
                        _log.LogMessage("Connected", "Connected, version " + version + ", mode " + modo);
                        _connected = true;
                    }
                    catch
                    {
                        _link.Close();
                        throw;
                    }
                }
                else
                {
                    _link.Close();
                    _connected = false;
                }
            }
        }

        public bool CanReverse { get { return true; } }

        public bool Reverse
        {
            // Se lee y escribe directamente en la config del equipo (clave
            // rotator.reverse en NVS), que es la misma que usan la web y ALPACA.
            // Sin copia local, no hay dos estados que puedan divergir. El
            // firmware lo aplica en caliente invirtiendo el pin DIR.
            get
            {
                if (!_connected) throw new NotConnectedException("Reverse");
                var rot = LeerSeccionRotador(LeerConfigEquipo());
                object v;
                return rot != null && rot.TryGetValue("reverse", out v) && v != null && Convert.ToBoolean(v);
            }
            set
            {
                if (!_connected) throw new NotConnectedException("Reverse");
                var raiz = LeerConfigEquipo();
                var rot = LeerSeccionRotador(raiz);
                if (rot == null)
                {
                    rot = new Dictionary<string, object>();
                    raiz["rotator"] = rot;
                }
                rot["reverse"] = value;
                if (!_link.SetConfigJson(new JavaScriptSerializer().Serialize(raiz)))
                    throw new DriverException("Device rejected the configuration");
                _log.LogMessage("Reverse", value.ToString());
            }
        }

        private Dictionary<string, object> LeerConfigEquipo()
        {
            return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(_link.GetConfigJson())
                   ?? new Dictionary<string, object>();
        }

        private static Dictionary<string, object> LeerSeccionRotador(Dictionary<string, object> raiz)
        {
            object rot;
            return raiz.TryGetValue("rotator", out rot) ? rot as Dictionary<string, object> : null;
        }

        public bool IsMoving
        {
            get
            {
                if (!_connected) throw new NotConnectedException("IsMoving");
                return _link.IsMoving();
            }
        }

        public float Position
        {
            get
            {
                if (!_connected) throw new NotConnectedException("Position");
                return (float)PasosAGrados(_link.GetPosition());
            }
        }

        /// <summary>
        /// Sin desfase de sincronización propio: el equipo no guarda uno, así
        /// que la posición mecánica y la del cielo son la misma. Sync() mueve
        /// directamente el contador del equipo.
        /// </summary>
        public float MechanicalPosition { get { return Position; } }

        public float TargetPosition
        {
            get
            {
                if (!_connected) throw new NotConnectedException("TargetPosition");
                return (float)PasosAGrados(_link.GetTarget());
            }
        }

        public float StepSize { get { return (float)(1.0 / _pasosPorGrado); } }

        public void Halt()
        {
            if (!_connected) throw new NotConnectedException("Halt");
            _link.Halt();
        }

        /// <summary>Movimiento relativo, en grados.</summary>
        public void Move(float position)
        {
            if (!_connected) throw new NotConnectedException("Move");
            MoveAbsolute((float)Normalizar(Position + position));
        }

        public void MoveAbsolute(float position)
        {
            if (!_connected) throw new NotConnectedException("MoveAbsolute");
            if (position < 0 || position > 360)
                throw new InvalidValueException("MoveAbsolute", position.ToString(), "0..360");

            // El firmware ya elige el camino más corto al recibir :SN en modo rotador.
            _link.MoveTo(GradosAPasos(position));
        }

        public void MoveMechanical(float position)
        {
            // Sin desfase de sincronización, mecánica y cielo coinciden.
            MoveAbsolute(position);
        }

        public void Sync(float position)
        {
            if (!_connected) throw new NotConnectedException("Sync");
            if (position < 0 || position > 360)
                throw new InvalidValueException("Sync", position.ToString(), "0..360");

            _link.SyncTo(GradosAPasos(position));
        }

        public string Description { get { return DriverDescriptionText; } }

        public string DriverInfo
        {
            get { return DriverDescriptionText + " - Moonlite protocol over USB CDC (DTR/RTS asserted)"; }
        }

        // ASCOM define DriverVersion como "m.n": solo major y minor. Por eso
        // aqui va 2.0 y no el 2.0.1 del ensamblado y los instaladores.
        public string DriverVersion { get { return "2.0"; } }

        public short InterfaceVersion { get { return 3; } }

        public string Name { get { return "AstroK Rotator"; } }

        public ArrayList SupportedActions { get { return new ArrayList(); } }

        public string Action(string actionName, string actionParameters)
        {
            throw new ActionNotImplementedException(actionName);
        }

        public void CommandBlind(string command, bool raw)
        {
            if (!_connected) throw new NotConnectedException("CommandBlind");
            if (raw) _link.WriteRaw(command); else _link.Send(command);
        }

        public bool CommandBool(string command, bool raw)
        {
            throw new MethodNotImplementedException("CommandBool");
        }

        public string CommandString(string command, bool raw)
        {
            if (!_connected) throw new NotConnectedException("CommandString");
            if (!raw) return _link.Query(command);

            _link.WriteRaw(command);
            return _link.ReadToHash();
        }

        public void Dispose()
        {
            Connected = false;
            if (_log != null)
            {
                _log.Enabled = false;
                _log.Dispose();
                _log = null;
            }
        }

        public void SetupDialog()
        {
            if (_connected)
            {
                System.Windows.Forms.MessageBox.Show(Textos.Get("msg.desconectaAntes"));
                return;
            }

            using (var form = new SetupDialogForm(_comPort, DeviceLink.MaxPasosEnfocador, false, true))
            {
                if (form.ShowDialog() == System.Windows.Forms.DialogResult.OK)
                {
                    _comPort = form.ComPort;
                    WriteProfile();
                }
            }
        }

        #endregion

        #region Perfil ASCOM y registro COM

        private void ReadProfile()
        {
            _comPort = ComPortDefault;
            try
            {
                using (var profile = new Profile { DeviceType = "Rotator" })
                {
                    if (!profile.IsRegistered(DriverId)) return;
                    _comPort = profile.GetValue(DriverId, ComPortProfileName, string.Empty, ComPortDefault);
                }
            }
            catch (Exception ex)
            {
                _log.LogMessage("ReadProfile", "No profile, using defaults: " + ex.Message);
            }
        }

        private void WriteProfile()
        {
            using (var profile = new Profile { DeviceType = "Rotator" })
            {
                profile.WriteValue(DriverId, ComPortProfileName, _comPort);
            }
        }

        [ComRegisterFunction]
        public static void RegisterASCOM(Type t)
        {
            using (var profile = new Profile { DeviceType = "Rotator" })
            {
                profile.Register(DriverId, DriverDescriptionText);
            }
        }

        [ComUnregisterFunction]
        public static void UnregisterASCOM(Type t)
        {
            using (var profile = new Profile { DeviceType = "Rotator" })
            {
                profile.Unregister(DriverId);
            }
        }

        #endregion
    }
}
