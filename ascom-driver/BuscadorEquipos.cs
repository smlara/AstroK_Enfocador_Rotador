using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Management;

namespace ASCOM.AstroK
{
    /// <summary>Un AstroK encontrado en un puerto, con lo que contestó al sondeo.</summary>
    public sealed class EquipoEncontrado
    {
        public string Puerto;    // "COM7"
        public string Modo;      // "FOCUSER" o "ROTATOR", tal cual lo da :DM#
        public string Firmware;  // "1.2", o vacío si el firmware es anterior a :DV#

        /// <summary>Lo que se ve en el desplegable.</summary>
        public override string ToString()
        {
            string etiqueta = Modo == "ROTATOR" ? "AstroK Rotator" : "AstroK Focuser";
            return string.IsNullOrEmpty(Firmware)
                ? Puerto + "  -  " + etiqueta
                : Puerto + "  -  " + etiqueta + " " + Firmware;
        }
    }

    /// <summary>
    /// Localiza equipos AstroK entre los puertos serie del sistema.
    ///
    /// Antes la ventana ofrecía un COM5 fijo que podía no existir, y la lista
    /// era la de todos los puertos del sistema sin distinguir cuál era el
    /// equipo. Aquí se enumeran los puertos reales, se descartan los que no
    /// cuelgan de USB (los COM de placa base y los virtuales de Bluetooth solo
    /// hacen perder tiempo en el sondeo) y se pregunta a cada uno quién es.
    /// </summary>
    public static class BuscadorEquipos
    {
        // El equipo contesta durante su propio arranque, pero conviene dar
        // margen: abrir el puerto resetea el ESP32 (USB nativo).
        private const int EsperaTrasAbrirMs = 400;
        private const int TimeoutSondeoMs = 1000;

        /// <summary>
        /// Puertos serie presentes que cuelgan de USB, en orden natural (COM2
        /// antes que COM10). Si WMI no responde no se descarta nada: más vale
        /// sondear de más que esconder el puerto bueno.
        /// </summary>
        public static List<string> PuertosUsb()
        {
            string[] todos = SerialPort.GetPortNames()
                                       .Select(p => p.Trim())
                                       .Distinct()
                                       .ToArray();

            HashSet<string> usb = null;
            try
            {
                usb = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

                // Los puertos aparecen en el nombre del dispositivo PnP como
                // "Silicon Labs CP210x ... (COM7)". Filtrar por ese nombre en
                // la propia consulta evita recorrer el árbol PnP entero.
                string consulta = "SELECT Name, PNPDeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'";
                using (var busqueda = new ManagementObjectSearcher(consulta))
                {
                    foreach (ManagementObject dispositivo in busqueda.Get())
                    {
                        string id = Convert.ToString(dispositivo["PNPDeviceID"]);
                        if (id == null || !id.StartsWith("USB\\", StringComparison.OrdinalIgnoreCase)) continue;

                        string puerto = ExtraerPuerto(Convert.ToString(dispositivo["Name"]));
                        if (puerto != null) usb.Add(puerto);
                    }
                }

                if (usb.Count == 0) usb = null; // WMI no ha visto nada: no filtrar
            }
            catch
            {
                usb = null; // sin WMI, o sin permisos: seguimos con todos
            }

            var resultado = usb == null ? todos.ToList() : todos.Where(usb.Contains).ToList();
            resultado.Sort((a, b) => Numero(a).CompareTo(Numero(b)));
            return resultado;
        }

        /// <summary>
        /// Sondea los puertos y devuelve solo los equipos del tipo pedido.
        ///
        /// Enfocador y rotador son productos distintos con su propia
        /// aplicación, y el jumper decide con cuál arranca la electrónica: un
        /// equipo que haya arrancado en el otro modo no vale aquí, así que no
        /// se devuelve. Igual que los puertos que no contestan, los que
        /// contestan otra cosa y los que ya tiene abiertos otro programa.
        /// </summary>
        public static List<EquipoEncontrado> Buscar(bool rotador)
        {
            string modoBuscado = rotador ? "ROTATOR" : "FOCUSER";
            var encontrados = new List<EquipoEncontrado>();

            foreach (string puerto in PuertosUsb())
            {
                EquipoEncontrado equipo = Sondear(puerto);
                if (equipo != null && equipo.Modo == modoBuscado) encontrados.Add(equipo);
            }

            return encontrados;
        }

        /// <summary>
        /// Pregunta a un puerto quién es. Devuelve null si no es un AstroK
        /// o si no se puede abrir.
        /// </summary>
        public static EquipoEncontrado Sondear(string puerto)
        {
            try
            {
                using (var serie = new SerialPort(puerto, 115200, Parity.None, 8, StopBits.One)
                {
                    ReadTimeout = TimeoutSondeoMs,
                    WriteTimeout = TimeoutSondeoMs,
                    // Mismo motivo que en DeviceLink: el USB Serial/JTAG del
                    // ESP32-C3 solo entrega datos con DTR asertado.
                    DtrEnable = true,
                    RtsEnable = true
                })
                {
                    serie.Open();
                    System.Threading.Thread.Sleep(EsperaTrasAbrirMs);
                    serie.DiscardInBuffer();

                    string modo = Preguntar(serie, "DM");
                    if (modo != "FOCUSER" && modo != "ROTATOR") return null;

                    // La versión es informativa: un firmware anterior a :DV#
                    // sigue siendo un equipo válido.
                    string firmware = Preguntar(serie, "DV");

                    return new EquipoEncontrado { Puerto = puerto, Modo = modo, Firmware = firmware };
                }
            }
            catch
            {
                // Puerto ocupado por otro programa, sin permisos, o un cacharro
                // que no habla este protocolo. En los tres casos no es lo que
                // buscamos. Que esté ocupado, además, nos protege de molestar a
                // un equipo que ya tenga conectado otra aplicación.
                return null;
            }
        }

        private static string Preguntar(SerialPort serie, string comando)
        {
            try
            {
                serie.DiscardInBuffer();
                serie.Write(":" + comando + "#");
                return serie.ReadTo("#").Trim().ToUpperInvariant();
            }
            catch (TimeoutException) { return ""; }
        }

        /// <summary>"Dispositivo serie USB (COM7)" -&gt; "COM7".</summary>
        private static string ExtraerPuerto(string nombre)
        {
            if (string.IsNullOrEmpty(nombre)) return null;

            int abre = nombre.LastIndexOf('(');
            int cierra = nombre.LastIndexOf(')');
            if (abre < 0 || cierra < abre) return null;

            string dentro = nombre.Substring(abre + 1, cierra - abre - 1).Trim();
            return dentro.StartsWith("COM", StringComparison.OrdinalIgnoreCase) ? dentro : null;
        }

        /// <summary>Para ordenar COM2 antes que COM10, que como texto van al revés.</summary>
        private static int Numero(string puerto)
        {
            int n;
            return int.TryParse(new string(puerto.Where(char.IsDigit).ToArray()), out n) ? n : int.MaxValue;
        }
    }
}
