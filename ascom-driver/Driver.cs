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
    /// Driver ASCOM de enfocador para AstroK (AstroRoEn) sobre su protocolo
    /// serie por USB.
    ///
    /// Existe por un motivo muy concreto: el ESP32-C3 usa su periférico USB
    /// Serial/JTAG, que solo entrega datos cuando el host aserta DTR. La clase
    /// SerialPort de .NET tiene DtrEnable y RtsEnable en false por defecto, así
    /// que los drivers serie genéricos abren el puerto y no reciben un solo
    /// byte. Ver OpenPort(): ahí está toda la diferencia.
    /// </summary>
    [Guid("5f5a2d7c-9c1e-4f7a-9d3b-1a2c4e6f8b90")]
    [ClassInterface(ClassInterfaceType.None)]
    [ComVisible(true)]
    public class Focuser : IFocuserV3
    {
        internal const string DriverId = "ASCOM.AstroK.Focuser";
        private const string DriverDescriptionText = "AstroK Focuser";

        // Perfil ASCOM
        private const string ComPortProfileName = "COM Port";
        private const string ComPortDefault = "COM5";
        private const string MaxStepProfileName = "Max Step";
        private static readonly string MaxStepDefault = DeviceLink.MaxPasosEnfocador.ToString();

        // Tope del recorrido que admite el firmware; el límite real es el
        // max_steps del equipo.
        private const int ProtocolMaxPosition = DeviceLink.MaxPasosEnfocador;

        // El transporte (incluido el DTR/RTS que hace falta con el USB nativo
        // del C3) vive en DeviceLink, compartido con el diálogo de configuración.
        private readonly DeviceLink _link = new DeviceLink();
        private bool _connected;
        private string _comPort;
        private int _maxStep;
        private TraceLogger _log;

        public Focuser()
        {
            _log = new TraceLogger("", "AstroK");
            ReadProfile();
            _log.Enabled = true;
            _log.LogMessage("Focuser", "Driver started");
        }

        #region Conexión

        /// <summary>
        /// Lee del equipo su max_steps configurado. Es la fuente de verdad: el
        /// valor del perfil ASCOM solo se usa si el equipo no lo reporta.
        /// </summary>
        private void LeerMaxStepDelEquipo()
        {
            try
            {
                string json = _link.GetConfigJson();
                var raiz = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);

                object focuser;
                if (raiz != null && raiz.TryGetValue("focuser", out focuser) &&
                    focuser is Dictionary<string, object>)
                {
                    object max;
                    if (((Dictionary<string, object>)focuser).TryGetValue("max_steps", out max) && max != null)
                    {
                        _maxStep = Convert.ToInt32(max);
                        _log.LogMessage("Connected", "Device max_steps: " + _maxStep);
                    }
                }
            }
            catch (Exception ex)
            {
                // Firmware antiguo sin la extensión :CG#: seguimos con el perfil.
                _log.LogMessage("Connected", "Could not read device config: " + ex.Message);
            }
        }

        #endregion

        #region IFocuserV3

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
                        // Handshake: :GV# debe responder la versión del firmware.
                        string version = _link.GetVersion();
                        if (string.IsNullOrEmpty(version))
                            throw new DriverException("Device does not respond to :GV#");

                        // El jumper decide el modo. Enfocador y rotador son
                        // productos distintos: si el equipo arrancó como
                        // rotador, este driver daría posiciones sin sentido.
                        string modo = _link.GetDeviceMode();
                        if (modo == "ROTATOR")
                            throw new DriverException(
                                "El equipo ha arrancado como ROTADOR, no como enfocador. " +
                                "Revisa el jumper de modo, o usa el driver AstroK Rotator.");

                        _log.LogMessage("Connected", "Connected, version " + version + ", mode " + modo);
                        LeerMaxStepDelEquipo();
                        _connected = true;
                    }
                    catch
                    {
                        // Si el handshake falla hay que soltar el puerto: dejarlo
                        // abierto lo bloquea para el siguiente intento.
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

        public bool Absolute { get { return true; } }

        // DeviceLink ya no lanza excepciones de ASCOM (para que la herramienta
        // independiente no dependa de la plataforma), así que es aquí donde se
        // comprueba el estado y se lanza lo que el cliente ASCOM espera.
        public int Position
        {
            get
            {
                if (!_connected) throw new NotConnectedException("Position");
                return _link.GetPosition();
            }
        }

        public bool IsMoving
        {
            get
            {
                if (!_connected) throw new NotConnectedException("IsMoving");
                return _link.IsMoving();
            }
        }

        public int MaxStep { get { return Math.Min(_maxStep, ProtocolMaxPosition); } }

        public int MaxIncrement { get { return MaxStep; } }

        public double StepSize { get { throw new PropertyNotImplementedException("StepSize", false); } }

        public bool TempComp
        {
            get { return false; }
            set { throw new PropertyNotImplementedException("TempComp", true); }
        }

        public bool TempCompAvailable { get { return false; } }

        public double Temperature { get { throw new PropertyNotImplementedException("Temperature", false); } }

        public void Move(int position)
        {
            if (!_connected) throw new NotConnectedException("Move");
            if (position < 0 || position > MaxStep)
                throw new InvalidValueException("Move", position.ToString(), "0.." + MaxStep);

            _link.MoveTo(position);
        }

        public void Halt()
        {
            if (!_connected) throw new NotConnectedException("Halt");
            _link.Halt();
        }

        public bool Link { get { return Connected; } set { Connected = value; } }

        public string Description { get { return DriverDescriptionText; } }

        public string DriverInfo
        {
            get { return DriverDescriptionText + " - serial protocol over USB CDC (DTR/RTS asserted)"; }
        }

        // ASCOM define DriverVersion como "m.n": solo major y minor. Por eso
        // aqui va 2.0 y no el 2.0.1 del ensamblado y los instaladores.
        public string DriverVersion { get { return "2.1"; } }

        public short InterfaceVersion { get { return 3; } }

        public string Name { get { return "AstroK"; } }

        public ArrayList SupportedActions { get { return new ArrayList(); } }

        public string Action(string actionName, string actionParameters)
        {
            throw new ActionNotImplementedException(actionName);
        }

        public void CommandBlind(string command, bool raw)
        {
            if (!_connected) throw new NotConnectedException("CommandBlind");

            // raw = la cadena va tal cual; si no, se envuelve en :...#
            if (raw) _link.WriteRaw(command);
            else _link.Send(command);
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

            using (var form = new SetupDialogForm(_comPort, _maxStep))
            {
                if (form.ShowDialog() == System.Windows.Forms.DialogResult.OK)
                {
                    _comPort = form.ComPort;
                    _maxStep = form.MaxStep;
                    WriteProfile();
                }
            }
        }

        #endregion

        #region Perfil ASCOM y registro COM

        private void ReadProfile()
        {
            // Valores por defecto primero: si el driver todavía no está
            // registrado en el perfil ASCOM, Profile.GetValue() lanza
            // DriverNotRegisteredException y no queremos que eso tumbe el
            // constructor (pasa al instanciarlo antes de registrarlo).
            _comPort = ComPortDefault;
            _maxStep = Convert.ToInt32(MaxStepDefault);

            try
            {
                using (var profile = new Profile { DeviceType = "Focuser" })
                {
                    if (!profile.IsRegistered(DriverId)) return;

                    _comPort = profile.GetValue(DriverId, ComPortProfileName, string.Empty, ComPortDefault);
                    _maxStep = Convert.ToInt32(profile.GetValue(DriverId, MaxStepProfileName, string.Empty, MaxStepDefault));
                }
            }
            catch (Exception ex)
            {
                _log.LogMessage("ReadProfile", "No profile, using defaults: " + ex.Message);
            }
        }

        private void WriteProfile()
        {
            using (var profile = new Profile { DeviceType = "Focuser" })
            {
                profile.WriteValue(DriverId, ComPortProfileName, _comPort);
                profile.WriteValue(DriverId, MaxStepProfileName, _maxStep.ToString());
            }
        }

        [ComRegisterFunction]
        public static void RegisterASCOM(Type t)
        {
            using (var profile = new Profile { DeviceType = "Focuser" })
            {
                profile.Register(DriverId, DriverDescriptionText);
            }
        }

        [ComUnregisterFunction]
        public static void UnregisterASCOM(Type t)
        {
            using (var profile = new Profile { DeviceType = "Focuser" })
            {
                profile.Unregister(DriverId);
            }
        }

        #endregion
    }
}
