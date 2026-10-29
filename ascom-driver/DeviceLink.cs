using System;
using System.Globalization;
using System.IO.Ports;
using System.Text;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Capa de transporte con el dispositivo: protocolo Moonlite más las
    /// extensiones AstroK (:CG#, :CS#, :CR#, :WI#, :WR#).
    ///
    /// La usan tanto el driver como la ventana de configuración, que necesita
    /// hablar con el equipo estando el driver desconectado.
    ///
    /// No depende de ASCOM a propósito: así la herramienta independiente
    /// funciona en un equipo sin ASCOM Platform instalado. El driver traduce
    /// estas excepciones a las de ASCOM donde hace falta.
    /// </summary>
    public class DeviceLink : IDisposable
    {
        private SerialPort _port;
        private readonly object _lock = new object();

        public bool IsOpen
        {
            get { lock (_lock) { return _port != null && _port.IsOpen; } }
        }

        public void Open(string comPort)
        {
            lock (_lock)
            {
                Close();

                var port = new SerialPort(comPort, 115200, Parity.None, 8, StopBits.One)
                {
                    // :CG# devuelve ~500 caracteres de base64, de ahí el margen.
                    ReadTimeout = 3000,
                    WriteTimeout = 3000,
                    // ---- LO IMPORTANTE ----
                    // El USB Serial/JTAG del ESP32-C3 solo entrega datos con DTR
                    // asertado. Sin estas dos líneas el equipo queda mudo: es
                    // exactamente lo que le pasa a los drivers Moonlite genéricos.
                    DtrEnable = true,
                    RtsEnable = true
                };

                port.Open();

                // Abrir el puerto resetea el ESP32 (USB nativo). El firmware
                // atiende el protocolo durante su arranque, pero damos margen y
                // descartamos cualquier resto del banner.
                System.Threading.Thread.Sleep(300);
                port.DiscardInBuffer();

                _port = port;
            }
        }

        public void Close()
        {
            lock (_lock)
            {
                if (_port == null) return;
                try { if (_port.IsOpen) _port.Close(); } catch { }
                _port.Dispose();
                _port = null;
            }
        }

        /// <summary>Envía :cmd# y devuelve la respuesta sin el '#' final.</summary>
        public string Query(string command)
        {
            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                _port.DiscardInBuffer();
                _port.Write(":" + command + "#");
                return _port.ReadTo("#");
            }
        }

        /// <summary>Envía un comando que no genera respuesta.</summary>
        public void Send(string command)
        {
            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                _port.Write(":" + command + "#");
            }
        }

        public void WriteRaw(string text)
        {
            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                _port.Write(text);
            }
        }

        public string ReadToHash()
        {
            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                return _port.ReadTo("#");
            }
        }

        // ---- Moonlite ----

        /// <summary>
        /// Tope del recorrido del enfocador, igual que MAX_FOCUSER_STEPS en el
        /// firmware. Moonlite lo dejaba en 65535 porque manda 4 cifras hex; el
        /// firmware ahora manda las que hagan falta, y aquí se leen y escriben
        /// enteras ("X4" pone 4 como mínimo, no como máximo).
        /// </summary>
        public const int MaxPasosEnfocador = 999999;

        public string GetVersion() { return Query("GV"); }

        public int GetPosition() { return ParseHex(Query("GP")); }

        public int GetTarget() { return ParseHex(Query("GN")); }

        public bool IsMoving() { return Query("GI").Trim() != "00"; }

        public void MoveTo(int position)
        {
            Send("SN" + position.ToString("X4"));
            Send("FG");
        }

        /// <summary>Fija la posición actual sin mover el motor (sync).</summary>
        public void SyncTo(int position) { Send("SP" + position.ToString("X4")); }

        public void Halt() { Send("FQ"); }

        // ---- Extensiones AstroK ----

        /// <summary>Estado de red tal cual lo reporta el equipo: "AP:192.168.4.1".</summary>
        public string GetNetworkStatus() { return Query("WI"); }

        /// <summary>
        /// Modo en que arrancó el equipo, fijado por el jumper: "FOCUSER" o
        /// "ROTATOR". Enfocador y rotador son productos distintos con su propio
        /// driver, así que conviene comprobarlo antes de dar por buena una
        /// conexión. Devuelve cadena vacía si el firmware es antiguo y no
        /// conoce el comando.
        /// </summary>
        public string GetDeviceMode()
        {
            try { return Query("DM").Trim().ToUpperInvariant(); }
            catch { return ""; }
        }

        /// <summary>
        /// Versión completa del firmware ("1.2"), vía :DV#. El :GV# de Moonlite
        /// solo da dos dígitos ("12") porque el protocolo lo define así, y eso
        /// es lo que se veía antes en la ventana. Devuelve cadena vacía si el
        /// firmware es anterior a :DV#.
        /// </summary>
        public string GetFirmwareVersion()
        {
            try { return Query("DV").Trim(); }
            catch { return ""; }
        }

        /// <summary>Banderas de DRV_STATUS en texto, para diagnóstico.</summary>
        public string GetDriverStatus() { return Query("DS"); }

        /// <summary>
        /// Para qué se compiló el firmware, vía :DH# ("35BY48;5"): el motor,
        /// que es el que aparece en el nombre de los .bin, y el chip_id de la
        /// cabecera de imagen ESP32 (5 = C3, 9 = S3). Devuelve false si el
        /// firmware es anterior a :DH#, que es también anterior a poder
        /// actualizarse por USB.
        /// </summary>
        public bool GetHardware(out string motor, out int chipId)
        {
            motor = "";
            chipId = -1;
            string r;
            try { r = Query("DH").Trim(); }
            catch { return false; }

            int sep = r.IndexOf(';');
            if (sep <= 0 || !int.TryParse(r.Substring(sep + 1), NumberStyles.Integer, CultureInfo.InvariantCulture, out chipId))
                return false;
            motor = r.Substring(0, sep);
            return true;
        }

        // ---- Actualización de firmware (:UB# :UD# :UE# :UA# :US#) ----

        private const int TrozoFirmware = 1024;
        private const int ReintentosPorTrozo = 5;
        // Lo más que acepta :UF# de una vez.
        private const int VacioMaximo = 0x10000;

        private static int ContarVacios(byte[] imagen, int inicio, int maximo)
        {
            int fin = Math.Min(imagen.Length, inicio + maximo);
            int i = inicio;
            while (i < fin && imagen[i] == 0xFF) i++;
            return i - inicio;
        }

        /// <summary>
        /// Graba una imagen de aplicación en la partición OTA libre del equipo
        /// y lo reinicia con ella. Si algo falla a medias se cancela y el
        /// equipo sigue con el firmware que tenía: hasta el :UE# final no se
        /// toca la partición de arranque.
        ///
        /// Todo va dentro del mismo lock: ninguna otra consulta (la posición
        /// que refresca la ventana, por ejemplo) puede colarse entre un trozo
        /// y su OK y desordenar las respuestas.
        ///
        /// Al volver, el equipo se está reiniciando y el puerto deja de valer.
        /// </summary>
        public void UpdateFirmware(byte[] imagen, Action<int, int> progreso)
        {
            Actualizar(imagen, progreso, false);
        }

        /// <summary>
        /// Lo mismo para la imagen LittleFS de la web (:UB...,W#). Solo la
        /// entiende el firmware 1.4.2 o posterior; uno anterior contesta
        /// ER:PARAMS. También reinicia el equipo al terminar.
        /// </summary>
        public void UpdateWeb(byte[] imagen, Action<int, int> progreso)
        {
            Actualizar(imagen, progreso, true);
        }

        private void Actualizar(byte[] imagen, Action<int, int> progreso, bool web)
        {
            string md5;
            using (var h = System.Security.Cryptography.MD5.Create()) md5 = CatalogoFirmware.Hex(h.ComputeHash(imagen));

            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                int timeoutOriginal = _port.ReadTimeout;
                try
                {
                    _port.ReadTimeout = 3000;
                    PedirOk("UB" + imagen.Length.ToString(CultureInfo.InvariantCulture) + "," + md5 + (web ? ",W" : ""));

                    int offset = 0;
                    while (offset < imagen.Length)
                    {
                        // La web es casi toda flash vacía: los tramos a 0xFF
                        // van con :UF# sin mandar los bytes. Solo para la web:
                        // el firmware anterior a 1.4.2 no conoce :UF#, y el que
                        // se actualiza con el programa puede ser ese.
                        int vacios = web ? ContarVacios(imagen, offset, VacioMaximo) : 0;
                        if (vacios >= TrozoFirmware)
                        {
                            vacios -= vacios % TrozoFirmware;
                            EnviarTrozo("UF" + offset.ToString("X", CultureInfo.InvariantCulture) + "," + vacios.ToString("X", CultureInfo.InvariantCulture),
                                        offset, vacios, ref offset);
                        }
                        else
                        {
                            int n = Math.Min(TrozoFirmware, imagen.Length - offset);
                            EnviarTrozo("UD" + offset.ToString("X", CultureInfo.InvariantCulture) + "," + Convert.ToBase64String(imagen, offset, n),
                                        offset, n, ref offset);
                        }
                        if (progreso != null) progreso(offset, imagen.Length);
                    }

                    // :UE# comprueba el MD5 de toda la imagen antes de contestar.
                    _port.ReadTimeout = 15000;
                    PedirOk("UE");
                }
                catch
                {
                    try { _port.ReadTimeout = 2000; _port.DiscardInBuffer(); _port.Write(":UA#"); } catch { }
                    throw;
                }
                finally
                {
                    try { if (_port != null) _port.ReadTimeout = timeoutOriginal; } catch { }
                }
            }
        }

        /// <summary>
        /// Manda un trozo y avanza offset. Si se queda sin respuesta, pregunta
        /// con :US# cuánto lleva escrito el equipo y sigue desde ahí: así no
        /// importa si se perdió la petición o solo el OK.
        /// </summary>
        private void EnviarTrozo(string cmd, int inicio, int n, ref int offset)
        {
            for (int intento = 1; ; intento++)
            {
                string r;
                try
                {
                    _port.DiscardInBuffer();
                    _port.Write(":" + cmd + "#");
                    r = _port.ReadTo("#").Trim();
                }
                catch (TimeoutException) { r = null; }

                if (r == "OK") { offset = inicio + n; return; }
                if (r != null && r.StartsWith("ER:") && r != "ER:BASE64")
                    throw new InvalidOperationException("Firmware update rejected by the device: " + r.Substring(3));
                if (intento >= ReintentosPorTrozo)
                    throw new TimeoutException("No answer from the device at offset 0x" + inicio.ToString("X"));

                // Sin respuesta (o trozo corrupto): ¿dónde está de verdad?
                int escrito;
                if (PreguntarEscrito(out escrito))
                {
                    if (escrito > inicio) { offset = escrito; return; }   // sí llegó, se perdió el OK
                    if (escrito < inicio)
                        throw new InvalidOperationException("Device is behind: 0x" + escrito.ToString("X"));
                }
            }
        }

        private bool PreguntarEscrito(out int escrito)
        {
            escrito = 0;
            try
            {
                _port.DiscardInBuffer();
                _port.Write(":US#");
                string r = _port.ReadTo("#").Trim();
                return int.TryParse(r, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out escrito);
            }
            catch (TimeoutException) { return false; }
        }

        private void PedirOk(string cmd)
        {
            _port.DiscardInBuffer();
            _port.Write(":" + cmd + "#");
            string r = _port.ReadTo("#").Trim();
            if (r != "OK")
                throw new InvalidOperationException("Firmware update rejected by the device: " +
                                                    (r.StartsWith("ER:") ? r.Substring(3) : r));
        }

        /// <summary>Configuración completa en JSON (la misma de /api/setup/config).</summary>
        public string GetConfigJson()
        {
            string b64 = Query("CG");
            return Encoding.UTF8.GetString(Convert.FromBase64String(b64.Trim()));
        }

        /// <summary>Guarda la configuración. Devuelve false si el equipo la rechaza.</summary>
        public bool SetConfigJson(string json)
        {
            string b64 = Convert.ToBase64String(Encoding.UTF8.GetBytes(json));

            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                _port.DiscardInBuffer();
                _port.Write(":CS" + b64 + "#");
                return _port.ReadTo("#").Trim() == "OK";
            }
        }

        /// <summary>
        /// Borra la configuración del equipo y lo reinicia con los valores de
        /// fábrica. Va por USB a propósito: cuando hace falta resetear suele
        /// ser porque no hay forma de llegar por red.
        /// </summary>
        public bool FactoryReset()
        {
            lock (_lock)
            {
                if (_port == null || !_port.IsOpen)
                    throw new InvalidOperationException("Port closed");

                _port.DiscardInBuffer();
                _port.Write(":CR#");
                try
                {
                    // Contesta OK# justo antes de reiniciar. Que se agote el
                    // tiempo significa que el firmware no conoce el comando: es
                    // anterior a esto y no ha borrado nada.
                    return _port.ReadTo("#").Trim() == "OK";
                }
                catch { return false; }
            }
        }

        /// <summary>Reinicia el equipo. Los cambios de WiFi solo se aplican al arrancar.</summary>
        public void Reboot()
        {
            lock (_lock)
            {
                if (_port == null || !_port.IsOpen) return;
                try
                {
                    _port.Write(":WR#");
                    // Contesta OK# y se reinicia; que falle la lectura es normal.
                    _port.ReadTo("#");
                }
                catch { }
            }
        }

        private static int ParseHex(string value)
        {
            return int.Parse(value.Trim(), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        }

        public void Dispose() { Close(); }
    }
}
