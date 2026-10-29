using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Una imagen de firmware lista para grabar, venga de un fichero local o de
    /// internet. Solo se construye si el nombre sigue el formato de los
    /// binarios publicados y el contenido es de verdad una imagen ESP32.
    ///
    /// El nombre es astrok_&lt;motor&gt;_v&lt;x.y.z&gt;_&lt;aaaammdd&gt;.bin, el que pone
    /// scripts/build_fw.ps1 y el mismo patrón que filtra fw.php en el
    /// servidor. No se acepta otro: del nombre salen el motor y la versión con
    /// los que se decide si la imagen es para este equipo.
    /// </summary>
    public sealed class ImagenFirmware
    {
        public static readonly Regex PatronNombre =
            new Regex(@"^astrok_(.+)_v(\d+\.\d+\.\d+)_(\d{4})(\d{2})(\d{2})\.bin$", RegexOptions.CultureInvariant);

        // Tamaño de las particiones app0/app1 de la tabla por defecto (1280 KB).
        // El firmware lo comprueba también, pero así se avisa antes de enviar.
        public const int TamanoMaximo = 0x140000;

        public string Nombre { get; private set; }
        public string Motor { get; private set; }
        public Version Version { get; private set; }
        public DateTime Fecha { get; private set; }
        public int ChipId { get; private set; }
        /// <summary>La imagen del programa, lo que va a la partición OTA.</summary>
        public byte[] Datos { get; private set; }
        /// <summary>La imagen LittleFS de la web, o null si el .bin no la lleva.</summary>
        public byte[] Web { get; private set; }

        // Desde 1.4.2 scripts/build_fw.ps1 pone la web detrás del programa y
        // cierra con 12 bytes: tamaño del programa y de la web (uint32 little
        // endian) y la marca "AKPK". Un .bin sin la marca es solo el programa.
        private static readonly byte[] MarcaPaquete = { (byte)'A', (byte)'K', (byte)'P', (byte)'K' };
        private const int TamanoCola = 12;

        // Partición spiffs de la tabla por defecto (1408 KB).
        public const int TamanoMaximoWeb = 0x160000;

        /// <summary>
        /// Valida nombre y contenido. Si algo falla devuelve null y deja en
        /// claveError la clave de Textos con el motivo.
        /// </summary>
        public static ImagenFirmware Crear(string nombre, byte[] datos, out string claveError)
        {
            byte[] web = null;
            if (datos != null && datos.Length > TamanoCola && TerminaEnMarca(datos))
            {
                int fin = datos.Length - TamanoCola;
                long tamApp = BitConverter.ToUInt32(datos, fin);
                long tamWeb = BitConverter.ToUInt32(datos, fin + 4);
                if (tamApp + tamWeb != fin || tamWeb == 0 || tamWeb > TamanoMaximoWeb)
                { claveError = "fw.errNoEsImagen"; return null; }
                web = new byte[tamWeb];
                Buffer.BlockCopy(datos, (int)tamApp, web, 0, (int)tamWeb);
                var app = new byte[tamApp];
                Buffer.BlockCopy(datos, 0, app, 0, (int)tamApp);
                datos = app;
            }

            claveError = null;
            Match m = PatronNombre.Match(nombre ?? "");
            if (!m.Success) { claveError = "fw.errNombre"; return null; }

            DateTime fecha;
            if (!DateTime.TryParseExact(m.Groups[3].Value + m.Groups[4].Value + m.Groups[5].Value, "yyyyMMdd",
                                        CultureInfo.InvariantCulture, DateTimeStyles.None, out fecha))
            { claveError = "fw.errNombre"; return null; }

            // Cabecera de imagen ESP32: byte 0 = 0xE9 y chip_id en los bytes
            // 12-13. Grabar la de otro chip deja el equipo en un bucle de
            // reinicios, así que esto no es una comprobación decorativa.
            if (datos == null || datos.Length < 24 || datos[0] != 0xE9) { claveError = "fw.errNoEsImagen"; return null; }
            if (datos.Length > TamanoMaximo) { claveError = "fw.errGrande"; return null; }

            return new ImagenFirmware
            {
                Nombre = nombre,
                Motor = m.Groups[1].Value,
                Version = Version.Parse(m.Groups[2].Value),
                Fecha = fecha,
                ChipId = datos[12] | (datos[13] << 8),
                Datos = datos,
                Web = web
            };
        }

        private static bool TerminaEnMarca(byte[] datos)
        {
            for (int i = 0; i < MarcaPaquete.Length; i++)
                if (datos[datos.Length - MarcaPaquete.Length + i] != MarcaPaquete[i]) return false;
            return true;
        }
    }

    /// <summary>Una entrada del catálogo de firmware publicado en internet.</summary>
    public sealed class FirmwareRemoto
    {
        public string Fichero;
        public string Url;
        public string Motor;
        public Version Version;
        public string Fecha;
        public long Tamano;
        public string Sha256;
        private object _descripcion;

        /// <summary>
        /// La descripción viene por idioma ({"es": ..., "en": ...}); las
        /// versiones antiguas de fw.php la mandaban como un texto único.
        /// </summary>
        public string Descripcion(string idioma)
        {
            var porIdioma = _descripcion as Dictionary<string, object>;
            if (porIdioma == null) return Convert.ToString(_descripcion ?? "");
            object t;
            if (porIdioma.TryGetValue(idioma, out t) && t != null) return Convert.ToString(t);
            if (porIdioma.TryGetValue("en", out t) && t != null) return Convert.ToString(t);
            foreach (object v in porIdioma.Values) return Convert.ToString(v);
            return "";
        }

        internal static FirmwareRemoto Desde(Dictionary<string, object> d)
        {
            Version v;
            string fichero = Texto(d, "file");
            if (!ImagenFirmware.PatronNombre.IsMatch(fichero) || !Version.TryParse(Texto(d, "version"), out v))
                return null;

            long tamano;
            long.TryParse(Texto(d, "size"), NumberStyles.Integer, CultureInfo.InvariantCulture, out tamano);
            object desc;
            d.TryGetValue("description", out desc);
            return new FirmwareRemoto
            {
                Fichero = fichero,
                Url = Texto(d, "url"),
                Motor = Texto(d, "motor"),
                Version = v,
                Fecha = Texto(d, "date"),
                Tamano = tamano,
                Sha256 = Texto(d, "sha256").ToLowerInvariant(),
                _descripcion = desc
            };
        }

        private static string Texto(Dictionary<string, object> d, string clave)
        {
            object v;
            return d.TryGetValue(clave, out v) && v != null ? Convert.ToString(v, CultureInfo.InvariantCulture) : "";
        }
    }

    /// <summary>Un instalador de la app publicado junto a los firmwares.</summary>
    public sealed class AppRemota
    {
        public string Fichero;
        public string Url;
        public string Producto;
        public Version Version;

        internal static AppRemota Desde(Dictionary<string, object> d)
        {
            Version version;
            string url = Texto(d, "url");
            // La URL se abre en el navegador: solo https, nada de file: ni similares.
            if (!Version.TryParse(Texto(d, "version"), out version) || !url.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
                return null;
            return new AppRemota { Fichero = Texto(d, "file"), Url = url, Producto = Texto(d, "product"), Version = version };
        }

        private static string Texto(Dictionary<string, object> d, string clave)
        {
            object v;
            return d.TryGetValue(clave, out v) && v != null ? Convert.ToString(v, CultureInfo.InvariantCulture) : "";
        }
    }

    /// <summary>
    /// Firmware publicado en internet. fw.php lista los .bin de su carpeta en
    /// JSON (ver bin/fw.php); aquí solo se lee esa lista y se descarga el
    /// elegido, comprobando tamaño y SHA-256 contra lo que dice la lista.
    /// </summary>
    public static class CatalogoFirmware
    {
        public const string UrlCatalogo = "https://astrok.eu/ascom/fw.php";
        private const int TimeoutMs = 15000;

        public static List<FirmwareRemoto> Consultar()
        {
            List<AppRemota> apps;
            return Consultar(out apps);
        }

        /// <summary>El catálogo entero: firmwares y, aparte, instaladores de la app.</summary>
        public static List<FirmwareRemoto> Consultar(out List<AppRemota> apps)
        {
            string json;
            using (var web = new ClienteWeb())
            {
                web.Headers[HttpRequestHeader.CacheControl] = "no-cache";
                json = web.DownloadString(UrlCatalogo);
            }

            var raiz = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
            var lista = new List<FirmwareRemoto>();
            object entradas;
            if (raiz != null && raiz.TryGetValue("firmwares", out entradas) && entradas is ArrayList)
            {
                foreach (object e in (ArrayList)entradas)
                {
                    var d = e as Dictionary<string, object>;
                    FirmwareRemoto f = d != null ? FirmwareRemoto.Desde(d) : null;
                    if (f != null) lista.Add(f);
                }
            }

            apps = new List<AppRemota>();
            if (raiz != null && raiz.TryGetValue("apps", out entradas) && entradas is ArrayList)
            {
                foreach (object e in (ArrayList)entradas)
                {
                    var d = e as Dictionary<string, object>;
                    AppRemota a = d != null ? AppRemota.Desde(d) : null;
                    if (a != null) apps.Add(a);
                }
            }
            return lista;
        }

        /// <summary>
        /// El instalador más nuevo publicado para este producto ("focuser" o
        /// "rotator"), si es más nuevo que la app que corre; si no, null.
        /// </summary>
        public static AppRemota AppMasNueva(List<AppRemota> apps, string producto, Version actual)
        {
            AppRemota mejor = null;
            foreach (AppRemota a in apps)
            {
                if (!string.Equals(a.Producto, producto, StringComparison.OrdinalIgnoreCase)) continue;
                if (mejor == null || a.Version > mejor.Version) mejor = a;
            }
            return (mejor != null && mejor.Version > actual) ? mejor : null;
        }

        /// <summary>Descarga y verifica. Lanza InvalidDataException si no cuadra con la lista.</summary>
        public static byte[] Descargar(FirmwareRemoto f)
        {
            byte[] datos;
            using (var web = new ClienteWeb()) datos = web.DownloadData(f.Url);

            if (f.Tamano > 0 && datos.Length != f.Tamano)
                throw new InvalidDataException(string.Format("{0} bytes, expected {1}", datos.Length, f.Tamano));
            if (!string.IsNullOrEmpty(f.Sha256))
            {
                string sha;
                using (var h = SHA256.Create()) sha = Hex(h.ComputeHash(datos));
                if (sha != f.Sha256) throw new InvalidDataException("SHA-256 mismatch");
            }
            return datos;
        }

        /// <summary>
        /// La más nueva publicada para este motor y este chip, si es más nueva
        /// que la que lleva el equipo; si no, null. Es lo que decide si la
        /// pestaña se ilumina.
        /// </summary>
        public static FirmwareRemoto MasNuevaPara(List<FirmwareRemoto> lista, string motor, Version actual)
        {
            FirmwareRemoto mejor = null;
            foreach (FirmwareRemoto f in lista)
            {
                if (!string.Equals(f.Motor, motor, StringComparison.OrdinalIgnoreCase)) continue;
                if (mejor == null || f.Version > mejor.Version) mejor = f;
            }
            return (mejor != null && (actual == null || mejor.Version > actual)) ? mejor : null;
        }

        public static string Hex(byte[] bytes)
        {
            var sb = new System.Text.StringBuilder(bytes.Length * 2);
            foreach (byte b in bytes) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }

        /// <summary>WebClient sin timeout configurable; este le pone uno.</summary>
        private sealed class ClienteWeb : WebClient
        {
            protected override WebRequest GetWebRequest(Uri address)
            {
                WebRequest r = base.GetWebRequest(address);
                if (r != null) r.Timeout = TimeoutMs;
                return r;
            }
        }
    }
}
