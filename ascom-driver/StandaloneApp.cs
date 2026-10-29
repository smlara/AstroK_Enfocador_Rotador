using System;
using System.Windows.Forms;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Punto de entrada de la herramienta independiente: abre la misma ventana
    /// de configuración que el driver, pero sin pasar por N.I.N.A. ni por el
    /// Chooser de ASCOM.
    ///
    /// No toca el perfil ASCOM ni necesita que la plataforma esté instalada:
    /// todo lo que hace va contra el equipo por USB. Sirve para configurar el
    /// enfocador en un portátil "limpio", o para tocar la WiFi sin arrancar la
    /// aplicación de captura.
    ///
    /// El puerto puede pasarse como argumento; si no, la ventana busca sola
    /// el equipo entre los puertos USB del sistema.
    /// </summary>
    public static class StandaloneApp
    {
        // Enfocador y rotador son productos distintos con su propio ejecutable.
        // El mismo fichero sirve para los dos: lo decide el símbolo de
        // compilación, así no hay dos copias del código que puedan divergir.
#if ROTATOR
        private const bool EsRotador = true;
#else
        private const bool EsRotador = false;
#endif

        [STAThread]
        public static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            string puerto = args.Length > 0 ? args[0] : "";

            try
            {
                Application.Run(new SetupDialogForm(puerto, DeviceLink.MaxPasosEnfocador, true, EsRotador));
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.ToString(), "AstroK",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
