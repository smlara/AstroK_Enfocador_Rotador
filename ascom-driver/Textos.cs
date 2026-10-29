using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Textos de la interfaz en varios idiomas.
    ///
    /// Los ficheros JSON van embebidos en el propio DLL (ver locales\*.json y
    /// el .csproj): así solo hay un archivo que instalar, sin subcarpetas de
    /// ensamblados satélite. Mismo planteamiento que los locales de la
    /// interfaz web del equipo.
    ///
    /// Los controles marcan su clave en la propiedad Tag; Aplicar() recorre el
    /// árbol y asigna el texto. Con eso, cambiar de idioma en caliente no
    /// obliga a reconstruir la ventana ni a guardar una referencia por control.
    /// </summary>
    public static class Textos
    {
        private const string IdiomaPorDefecto = "en";

        private static readonly string[] Disponibles = { "es", "en" };
        private static Dictionary<string, string> _actual;
        private static string _idioma;

        public static string Idioma { get { return _idioma ?? IdiomaPorDefecto; } }

        public static string[] Idiomas { get { return Disponibles; } }

        /// <summary>Nombre del idioma para mostrarlo en el selector.</summary>
        public static string NombreIdioma(string codigo)
        {
            switch (codigo)
            {
                case "es": return "Español";
                case "en": return "English";
                default: return codigo;
            }
        }

        static Textos()
        {
            // Por defecto, el idioma de Windows si lo tenemos; si no, inglés.
            string sistema = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            Cargar(Array.IndexOf(Disponibles, sistema) >= 0 ? sistema : IdiomaPorDefecto);
        }

        public static void Cargar(string codigo)
        {
            if (Array.IndexOf(Disponibles, codigo) < 0) codigo = IdiomaPorDefecto;

            var asm = Assembly.GetExecutingAssembly();
            string recurso = null;
            foreach (string nombre in asm.GetManifestResourceNames())
            {
                if (nombre.EndsWith("locales." + codigo + ".json", StringComparison.OrdinalIgnoreCase))
                {
                    recurso = nombre;
                    break;
                }
            }

            if (recurso == null)
            {
                _actual = new Dictionary<string, string>();
                _idioma = codigo;
                return;
            }

            using (Stream s = asm.GetManifestResourceStream(recurso))
            using (var lector = new StreamReader(s))
            {
                var json = new JavaScriptSerializer().Deserialize<Dictionary<string, string>>(lector.ReadToEnd());
                _actual = json ?? new Dictionary<string, string>();
            }
            _idioma = codigo;
        }

        /// <summary>
        /// Texto de una clave. Si falta, devuelve la propia clave entre
        /// corchetes: así una traducción olvidada se ve en pantalla en vez de
        /// dejar el control en blanco.
        /// </summary>
        public static string Get(string clave)
        {
            if (string.IsNullOrEmpty(clave)) return "";
            string valor;
            if (_actual != null && _actual.TryGetValue(clave, out valor)) return valor;
            return "[" + clave + "]";
        }

        /// <summary>
        /// Recorre el árbol de controles y asigna el texto a los que llevan una
        /// clave en Tag. Las pestañas también, que son controles con Text.
        /// </summary>
        public static void Aplicar(Control raiz)
        {
            var clave = raiz.Tag as string;
            if (!string.IsNullOrEmpty(clave)) raiz.Text = Get(clave);

            var tabs = raiz as TabControl;
            if (tabs != null)
            {
                foreach (TabPage p in tabs.TabPages) Aplicar(p);
            }

            foreach (Control hijo in raiz.Controls) Aplicar(hijo);
        }
    }
}
