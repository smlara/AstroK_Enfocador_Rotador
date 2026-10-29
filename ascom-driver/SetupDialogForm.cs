using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Ventana de configuración con tres pestañas: control del enfocador,
    /// parámetros del driver y ajustes de WiFi. Habla con el equipo por USB con
    /// su propia conexión, así que sirve tanto desde el driver ASCOM (con éste
    /// desconectado) como desde la herramienta independiente.
    ///
    /// La pestaña de WiFi existe para poder configurar la red por cable: por
    /// HTTP no se puede si precisamente la WiFi es lo que falla.
    ///
    /// Los textos no están escritos aquí: cada control lleva su clave en Tag y
    /// Textos.Aplicar() los rellena, que es lo que permite cambiar de idioma
    /// sin reconstruir la ventana.
    /// </summary>
    public class SetupDialogForm : Form
    {
        private readonly DeviceLink _link = new DeviceLink();
        private readonly JavaScriptSerializer _json = new JavaScriptSerializer();
        private readonly bool _standalone;
        // Enfocador y rotador son productos distintos y la ventana se adapta:
        // el rotador trabaja en grados y con pasos_por_grado, no en pasos con
        // un tope máximo.
        private readonly bool _rotador;
        private double _pasosPorGrado = 37.75;
        private Dictionary<string, object> _config;

        // --- Pestaña enfocador ---
        private ComboBox _comBox;
        private Button _btnBuscar;
        private string _puertoPreferido;
        private bool _buscando;
        private Button _btnConectar;
        private Label _lblEstado;
        private Label _lblPosicion;
        private TextBox _txtMoverA;
        private TextBox _txtFijarA;
        private NumericUpDown _numMax;
        private Timer _timer;

        // --- Pestaña avanzados ---
        private NumericUpDown _numCorriente;
        private ComboBox _cmbMicropasos;
        private CheckBox _chkInvertir;
        private NumericUpDown _numStall;
        private NumericUpDown _numStallMinVel;
        private NumericUpDown _numHoldCorriente;

        // --- Pestaña WiFi ---
        private Label _lblIp;
        private TextBox _txtApNombre;
        private TextBox _txtApPass;
        private NumericUpDown _numTimeout;
        private NumericUpDown _numRetries;
        private CheckBox _chkWifi;
        private DataGridView _gridRedes;

        private ComboBox _cmbIdioma;
        private Label _lblVersiones;
        private string _versionFirmware = "";
        private Control[] _controlesQuePidenConexion;

        /// <summary>
        /// Puerto elegido. El desplegable puede contener equipos ya
        /// identificados ("COM7  -  AstroK Rotator 1.2") o texto escrito a
        /// mano, asi que aqui se devuelve siempre el nombre del puerto pelado.
        /// </summary>
        public string ComPort
        {
            get
            {
                var equipo = _comBox.SelectedItem as EquipoEncontrado;
                if (equipo != null) return equipo.Puerto;

                string texto = ((_comBox.SelectedItem as string) ?? _comBox.Text ?? "").Trim();
                int espacio = texto.IndexOf(' ');
                return espacio > 0 ? texto.Substring(0, espacio) : texto;
            }
        }
        public int MaxStep { get { return (int)_numMax.Value; } }

        public SetupDialogForm(string comPort, int maxStep) : this(comPort, maxStep, false, false) { }

        public SetupDialogForm(string comPort, int maxStep, bool standalone) : this(comPort, maxStep, standalone, false) { }

        public SetupDialogForm(string comPort, int maxStep, bool standalone, bool rotador)
        {
            _standalone = standalone;
            _rotador = rotador;

            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;
            MinimizeBox = _standalone;
            ClientSize = new Size(620, 725);
            Tag = _rotador
                ? (_standalone ? "ventana.tituloStandaloneRot" : "ventana.tituloRot")
                : (_standalone ? "ventana.tituloStandalone" : "ventana.titulo");

            ConstruirBarraConexion(comPort);
            var tabs = new TabControl { Left = 10, Top = 75, Width = 600, Height = 590 };
            var tabFoco = new TabPage { Tag = _rotador ? "tab.rotador" : "tab.enfocador" };
            var tabAvanzado = new TabPage { Tag = "tab.avanzados" };
            var tabWifi = new TabPage { Tag = "tab.wifi" };
            _tabFirmware = new TabPage { Tag = "tab.firmware" };
            tabs.TabPages.Add(tabFoco);
            tabs.TabPages.Add(tabAvanzado);
            tabs.TabPages.Add(tabWifi);
            tabs.TabPages.Add(_tabFirmware);
            // Dibujadas a mano para poder iluminar la de firmware (DibujarPestana).
            _tabs = tabs;
            tabs.DrawMode = TabDrawMode.OwnerDrawFixed;
            tabs.SizeMode = TabSizeMode.Fixed;
            tabs.ItemSize = new Size(140, 22);
            tabs.DrawItem += DibujarPestana;

            var pendientes = new List<Control>();
            ConstruirPestanaFocuser(tabFoco, maxStep, pendientes);
            ConstruirPestanaAvanzados(tabAvanzado, pendientes);
            ConstruirPestanaWifi(tabWifi, pendientes);
            _controlesQuePidenConexion = pendientes.ToArray();
            ConstruirPestanaFirmware(_tabFirmware);

            Controls.Add(tabs);
            ConstruirPieDeVentana();

            _timer = new Timer { Interval = 500 };
            _timer.Tick += (s, e) => RefrescarPosicion();

            Textos.Aplicar(this);
            RetraducirFirmware();
            AplicarIcono();
            ActualizarHabilitacion();
            // Cerrar a mitad de una grabación dejaría el equipo esperando el
            // resto de la imagen (sigue con su firmware, pero sin saberlo nadie).
            FormClosing += (s, e) =>
            {
                if (!_grabando) return;
                e.Cancel = true;
                MessageBox.Show(this, Textos.Get("fw.noCerrar"), Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            };
            FormClosed += (s, e) => { _timer.Stop(); _link.Dispose(); };
        }

        /// <summary>
        /// Icono de la ventana, tomado del recurso embebido en el DLL. Así lo
        /// llevan igual la herramienta independiente y el diálogo del driver,
        /// sin depender de un archivo suelto junto al ejecutable.
        ///
        /// Cada producto embebe el suyo (con la F o la R), y como solo hay uno
        /// por ensamblado basta con buscar la extensión: así el nombre del
        /// recurso no queda cableado aquí.
        /// </summary>
        private void AplicarIcono()
        {
            try
            {
                var asm = System.Reflection.Assembly.GetExecutingAssembly();
                foreach (string nombre in asm.GetManifestResourceNames())
                {
                    if (!nombre.EndsWith(".ico", StringComparison.OrdinalIgnoreCase)) continue;
                    using (var s = asm.GetManifestResourceStream(nombre))
                    {
                        if (s != null) Icon = new Icon(s);
                    }
                    break;
                }
            }
            catch
            {
                // Sin icono se ve el de por defecto: no es motivo para no abrir.
            }
        }

        /// <summary>
        /// Versión de la app y, si hay conexión, la del firmware del equipo.
        /// La de la app sale del ensamblado para que no haya un segundo sitio
        /// que mantener sincronizado con AssemblyInfo.
        /// </summary>
        private void ActualizarVersiones()
        {
            Version v = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
            string app = string.Format("{0}.{1}.{2}", v.Major, v.Minor, v.Build);
            string fw = string.IsNullOrEmpty(_versionFirmware) ? "—" : _versionFirmware;
            _lblVersiones.Text = string.Format(Textos.Get("ventana.versiones"), app, fw);
        }

        private LinkLabel _lnkApp;
        private AppRemota _appNueva;

        /// <summary>
        /// Mira en el catálogo si hay un instalador más nuevo de esta app, en
        /// segundo plano y sin decir nada si no hay red.
        /// </summary>
        private void ComprobarAppNueva()
        {
            Version actual = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
            string producto = _rotador ? "rotator" : "focuser";
            System.Threading.Tasks.Task.Factory.StartNew(() =>
            {
                AppRemota nueva = null;
                try
                {
                    List<AppRemota> apps;
                    CatalogoFirmware.Consultar(out apps);
                    nueva = CatalogoFirmware.AppMasNueva(apps, producto, new Version(actual.Major, actual.Minor, actual.Build));
                }
                catch { }
                if (nueva == null || IsDisposed || !IsHandleCreated) return;
                try { BeginInvoke((Action)(() => { _appNueva = nueva; MostrarAppNueva(); })); }
                catch (ObjectDisposedException) { }
            });
        }

        private void MostrarAppNueva()
        {
            if (_appNueva == null) return;
            string texto = string.Format(Textos.Get("ventana.appNueva"), _appNueva.Version);
            _lnkApp.Text = texto;
            _lnkApp.LinkArea = new LinkArea(0, texto.Length);
            _lnkApp.Visible = true;
        }

        private void ConstruirPieDeVentana()
        {
            var lblIdioma = new Label { Tag = "ventana.idioma", Left = 12, Top = 684, Width = 60 };
            _cmbIdioma = new ComboBox
            {
                Left = 72, Top = 680, Width = 110, DropDownStyle = ComboBoxStyle.DropDownList
            };

            // Sin esto, la única versión a la vista era la que respondía :GV#,
            // que es el campo de dos dígitos de Moonlite y estaba fijo en "10".
            _lblVersiones = new Label { Left = 195, Top = 684, Width = 235, ForeColor = Color.DimGray };
            foreach (string c in Textos.Idiomas) _cmbIdioma.Items.Add(Textos.NombreIdioma(c));
            _cmbIdioma.SelectedIndex = Array.IndexOf(Textos.Idiomas, Textos.Idioma);
            _cmbIdioma.SelectedIndexChanged += (s, e) =>
            {
                Textos.Cargar(Textos.Idiomas[_cmbIdioma.SelectedIndex]);
                Textos.Aplicar(this);
                RenombrarColumnasRejilla();
                // El botón de conectar alterna su texto, así que no basta con
                // la clave fija del Tag: hay que recalcularlo.
                ActualizarTextoConectar();
                ActualizarVersiones();
                MostrarAppNueva();
                RetraducirFirmware();
            };
            Controls.Add(lblIdioma);
            Controls.Add(_cmbIdioma);
            Controls.Add(_lblVersiones);
            ActualizarVersiones();

            // Aviso de app nueva, debajo del pie; invisible hasta que la hay.
            _lnkApp = new LinkLabel
            {
                Left = 12, Top = 704, Width = 596, Height = 18, Visible = false,
                LinkColor = Color.DarkGoldenrod, ActiveLinkColor = Color.Goldenrod,
                Font = new Font(Font, FontStyle.Bold)
            };
            _lnkApp.LinkClicked += (s, e) =>
            {
                if (_appNueva == null) return;
                try { System.Diagnostics.Process.Start(_appNueva.Url); }
                catch (Exception ex) { MessageBox.Show(this, string.Format(Textos.Get("msg.error"), ex.Message), Text); }
            };
            Controls.Add(_lnkApp);

            if (_standalone)
            {
                var cerrar = new Button { Tag = "ventana.cerrar", Left = 525, Top = 680, Width = 85 };
                cerrar.Click += (s, e) => Close();
                Controls.Add(cerrar);
                CancelButton = cerrar;
            }
            else
            {
                var ok = new Button { Tag = "ventana.aceptar", Left = 435, Top = 680, Width = 85, DialogResult = DialogResult.OK };
                var cancel = new Button { Tag = "ventana.cancelar", Left = 525, Top = 680, Width = 85, DialogResult = DialogResult.Cancel };
                Controls.Add(ok);
                Controls.Add(cancel);
                AcceptButton = ok;
                CancelButton = cancel;
            }
        }

        #region Pestaña enfocador

        /// <summary>
        /// Puerto, buscar, conectar y estado. Va fuera de las pestañas porque
        /// sin equipo conectado las pestañas están deshabilitadas y vacías, y
        /// esto es lo único que hay que poder tocar entonces.
        /// </summary>
        private void ConstruirBarraConexion(string comPort)
        {
            var lblCom = new Label { Tag = "foco.puerto", Left = 15, Top = 18, Width = 85 };
            _comBox = new ComboBox { Left = 100, Top = 15, Width = 220, DropDownStyle = ComboBoxStyle.DropDown };
            // Arranca vacio a proposito. Antes se ofrecia un COM fijo del
            // perfil ASCOM aunque no hubiera nada conectado ahi; ahora en la
            // lista solo entra lo que el sondeo haya confirmado que es del tipo
            // de esta aplicacion. El puerto guardado sirve solo para decidir
            // cual queda elegido si aparece entre los encontrados.
            _puertoPreferido = comPort;

            var btnRefrescar = new Button { Tag = "foco.buscar", Left = 330, Top = 14, Width = 90 };
            btnRefrescar.Click += (s, e) => BuscarEquipos();

            _btnBuscar = btnRefrescar;

            _btnConectar = new Button { Left = 430, Top = 14, Width = 140 };
            _btnConectar.Click += (s, e) => AlternarConexion();

            _lblEstado = new Label { Tag = "foco.desconectado", Left = 15, Top = 48, Width = 595, ForeColor = Color.DimGray };

            Controls.AddRange(new Control[] { lblCom, _comBox, btnRefrescar, _btnConectar, _lblEstado });
        }

        private void ConstruirPestanaFocuser(TabPage tab, int maxStep, List<Control> pendientes)
        {
            _lblPosicion = new Label
            {
                Text = "—",
                Left = 15, Top = 10, Width = 555, Height = 34,
                Font = new Font(Font.FontFamily, 15, FontStyle.Bold),
                TextAlign = ContentAlignment.MiddleCenter
            };

            // Enfocador: los mismos saltos que el encoder físico, en pasos.
            // Rotador: grados, que es la magnitud en la que se piensa al
            // encuadrar. El texto es numérico y no necesita traducción.
            int[] saltos = _rotador
                ? new[] { -90, -10, -1, 1, 10, 90 }
                : new[] { -500, -100, -10, 10, 100, 500 };
            int x = 15;
            foreach (int salto in saltos)
            {
                int delta = salto;
                var b = new Button
                {
                    Text = (delta > 0 ? "+" : "") + delta + (_rotador ? "°" : ""),
                    Left = x, Top = 50, Width = 65
                };
                b.Click += (s, e) => MoverRelativo(delta);
                tab.Controls.Add(b);
                pendientes.Add(b);
                x += 69;
            }

            var btnParar = new Button { Tag = "foco.parar", Left = 15, Top = 85, Width = 555, Height = 28, ForeColor = Color.Firebrick };
            btnParar.Click += (s, e) => Proteger(() => _link.Halt());
            pendientes.Add(btnParar);

            var lblMover = new Label { Tag = _rotador ? "rot.moverA" : "foco.moverA", Left = 15, Top = 130, Width = 85 };
            _txtMoverA = new TextBox { Left = 100, Top = 127, Width = 90, Text = "0" };
            var btnMover = new Button { Tag = "foco.ir", Left = 196, Top = 126, Width = 60 };
            btnMover.Click += (s, e) =>
            {
                if (_rotador)
                {
                    double grados;
                    if (!LeerGrados(_txtMoverA.Text, out grados)) return;
                    Proteger(() => _link.MoveTo(GradosAPasos(grados)));
                    return;
                }
                int destino;
                if (!int.TryParse(_txtMoverA.Text, out destino)) return;
                Proteger(() => _link.MoveTo(Limitar(destino)));
            };
            pendientes.Add(btnMover);

            var lblFijar = new Label { Tag = _rotador ? "rot.fijarA" : "foco.fijarA", Left = 15, Top = 162, Width = 85, Height = 32 };
            _txtFijarA = new TextBox { Left = 100, Top = 165, Width = 90, Text = "0" };
            var btnFijar = new Button { Tag = "foco.aplicar", Left = 196, Top = 164, Width = 60 };
            btnFijar.Click += (s, e) =>
            {
                // Sync: cambia el contador sin mover el motor. Sirve para
                // recuperar la referencia si se ha perdido el cero.
                if (_rotador)
                {
                    double grados;
                    if (!LeerGrados(_txtFijarA.Text, out grados)) return;
                    Proteger(() => _link.SyncTo(GradosAPasos(grados)));
                    return;
                }
                int valor;
                if (!int.TryParse(_txtFijarA.Text, out valor)) return;
                Proteger(() => _link.SyncTo(Limitar(valor)));
            };
            pendientes.Add(btnFijar);

            var lblMax = new Label { Tag = _rotador ? "rot.pasosPorGrado" : "foco.posMax", Left = 15, Top = 210, Width = 110 };
            _numMax = new NumericUpDown
            {
                Left = 130, Top = 207, Width = 90,
                // El rotador necesita decimales: la reductora rara vez da un
                // número redondo de pasos por grado.
                DecimalPlaces = _rotador ? 2 : 0,
                Increment = _rotador ? 0.1M : 1M,
                Minimum = _rotador ? 0.01M : 1M,
                Maximum = DeviceLink.MaxPasosEnfocador,
                // El maxStep que llega es del enfocador; al rotador no le
                // aplica, así que parte de los pasos por grado por defecto
                // hasta que se lean los del equipo.
                Value = _rotador ? (decimal)_pasosPorGrado
                                 : Math.Max(1, Math.Min(DeviceLink.MaxPasosEnfocador, maxStep))
            };
            var btnGuardarMax = new Button { Tag = "foco.guardarEquipo", Left = 226, Top = 206, Width = 160 };
            btnGuardarMax.Click += (s, e) => GuardarMaxEnEquipo();
            pendientes.Add(btnGuardarMax);

            var nota = new Label { Tag = _rotador ? "rot.notaPasosPorGrado" : "foco.notaMax", Left = 15, Top = 236, Width = 555, Height = 30, ForeColor = Color.DimGray };

            tab.Controls.AddRange(new Control[] {
                _lblPosicion,
                btnParar, lblMover, _txtMoverA, btnMover, lblFijar, _txtFijarA, btnFijar,
                lblMax, _numMax, btnGuardarMax, nota
            });
        }

        // El protocolo transporta el rotador en PASOS con 32768 como cero
        // lógico (ver FocuserProtocol). La ventana trabaja en grados, así que
        // la conversión vive aquí.
        private const int CeroProtocolo = 32768;

        private static double NormalizarGrados(double g)
        {
            g = g % 360.0;
            return g < 0 ? g + 360.0 : g;
        }

        private double PasosAGrados(int pasos)
        {
            return NormalizarGrados((pasos - CeroProtocolo) / _pasosPorGrado);
        }

        private int GradosAPasos(double grados)
        {
            return CeroProtocolo + (int)Math.Round(NormalizarGrados(grados) * _pasosPorGrado);
        }

        /// <summary>
        /// Acota lo que se va a enviar. El enfocador tiene recorrido finito y
        /// se limita; el rotador da vueltas, así que se normaliza a 0-360.
        /// </summary>
        /// <summary>
        /// Lee unos grados tecleados a mano aceptando coma y punto como
        /// separador decimal.
        ///
        /// Con TryParse a secas manda la cultura de Windows, que trata el otro
        /// separador como agrupador de miles y no valida dónde cae: en español,
        /// "45.5" se convierte en 455, que normalizado son 95 grados. El
        /// rotador se iba a un ángulo que nadie había pedido, sin un mal aviso.
        /// Se descarta AllowThousands a propósito: un "1.234,5" ambiguo es
        /// mejor que no haga nada a que adivine.
        /// </summary>
        private static bool LeerGrados(string texto, out double grados)
        {
            grados = 0;
            if (string.IsNullOrEmpty(texto)) return false;

            return double.TryParse(texto.Trim().Replace(',', '.'),
                                   NumberStyles.Float, CultureInfo.InvariantCulture,
                                   out grados);
        }

        private int Limitar(int valor)
        {
            if (_rotador) return valor;
            if (valor < 0) return 0;
            if (valor > (int)_numMax.Value) return (int)_numMax.Value;
            return valor;
        }

        private void MoverRelativo(int delta)
        {
            Proteger(() =>
            {
                int actual = _link.GetPosition();
                if (_rotador)
                {
                    // delta viene en grados; el firmware ya elige el camino corto.
                    double destino = NormalizarGrados(PasosAGrados(actual) + delta);
                    _link.MoveTo(GradosAPasos(destino));
                }
                else
                {
                    _link.MoveTo(Limitar(actual + delta));
                }
            });
        }

        private void RefrescarPosicion()
        {
            if (!_link.IsOpen) return;
            try
            {
                int pos = _link.GetPosition();
                bool moviendo = _link.IsMoving();
                string texto = _rotador
                    ? PasosAGrados(pos).ToString("0.00") + "°"
                    : pos.ToString();
                _lblPosicion.Text = texto + (moviendo ? "  ▸" : "");
                _lblPosicion.ForeColor = moviendo ? Color.SeaGreen : Color.Black;
            }
            catch
            {
                // Un fallo puntual de lectura no debe tumbar la ventana; si la
                // conexión se ha perdido de verdad, el siguiente comando lo dirá.
            }
        }

        private void GuardarMaxEnEquipo()
        {
            Proteger(() =>
            {
                if (_config == null) _config = LeerConfig();
                var dev = ObtenerSeccion(_config, SeccionDispositivo);
                if (_rotador)
                {
                    _pasosPorGrado = (double)_numMax.Value;
                    dev["steps_per_degree"] = _pasosPorGrado;
                }
                else
                {
                    dev["max_steps"] = (int)_numMax.Value;
                }
                GuardarConfig("msg.guardadoEquipo");
            });
        }

        #endregion

        #region Pestaña avanzados

        private void ConstruirPestanaAvanzados(TabPage tab, List<Control> pendientes)
        {
            const int margen = 18;
            const int anchoTotal = 550;
            const int xCampo = 170;

            var intro = new Label { Tag = "avz.intro", Left = margen, Top = 15, Width = anchoTotal, ForeColor = Color.DimGray };

            var lblCorriente = new Label { Tag = "avz.corriente", Left = margen, Top = 50, Width = 140 };
            _numCorriente = new NumericUpDown
            {
                Left = xCampo, Top = 47, Width = 90,
                Minimum = 100, Maximum = 2000, Increment = 50, Value = 800
            };
            var notaCorriente = new Label { Tag = "avz.notaCorriente", Left = margen, Top = 75, Width = anchoTotal, Height = 18, ForeColor = Color.DimGray };

            var lblMicro = new Label { Tag = "avz.micropasos", Left = margen, Top = 105, Width = 140 };
            _cmbMicropasos = new ComboBox { Left = xCampo, Top = 102, Width = 90, DropDownStyle = ComboBoxStyle.DropDownList };
            foreach (int m in new[] { 1, 2, 4, 8, 16, 32, 64, 128, 256 }) _cmbMicropasos.Items.Add(m);
            _cmbMicropasos.SelectedItem = _rotador ? 8 : 4;

            // Invierte el pin DIR en el equipo; en el rotador es además la
            // propiedad Reverse de ASCOM, guardada en la misma clave.
            _chkInvertir = new CheckBox { Tag = "avz.invertir", Left = 300, Top = 103, Width = 200 };

            var sep = new Label { Left = margen, Top = 140, Width = anchoTotal, Height = 2, BorderStyle = BorderStyle.Fixed3D };

            var lblStallTitulo = new Label
            {
                Tag = "avz.stallTitulo", Left = margen, Top = 152, Width = anchoTotal,
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold)
            };

            var lblStall = new Label { Tag = "avz.sensibilidad", Left = margen, Top = 185, Width = 140 };
            _numStall = new NumericUpDown { Left = xCampo, Top = 182, Width = 90, Minimum = 0, Maximum = 255, Value = 100 };
            var notaStall = new Label { Tag = "avz.notaSensibilidad", Left = margen, Top = 210, Width = anchoTotal, Height = 18, ForeColor = Color.DimGray };

            var lblMinVel = new Label { Tag = "avz.velMinima", Left = margen, Top = 240, Width = 140 };
            _numStallMinVel = new NumericUpDown { Left = xCampo, Top = 237, Width = 90, Minimum = 50, Maximum = 5000, Increment = 50, Value = 400 };
            var notaMinVel = new Label { Tag = "avz.notaVelMinima", Left = margen, Top = 265, Width = anchoTotal, Height = 18, ForeColor = Color.DimGray };

            var explicacion = new Label { Tag = "avz.explicacion", Left = margen, Top = 300, Width = anchoTotal, Height = 95, ForeColor = Color.DimGray };

            var lblHold = new Label { Tag = "avz.corrienteHold", Left = margen, Top = 410, Width = 150 };
            _numHoldCorriente = new NumericUpDown
            {
                Left = xCampo, Top = 407, Width = 90,
                Minimum = 0, Maximum = 2000, Increment = 25, Value = 200
            };
            var notaHold = new Label { Tag = "avz.notaCorrienteHold", Left = margen, Top = 435, Width = anchoTotal, Height = 32, ForeColor = Color.DimGray };

            var btnGuardar = new Button { Tag = "avz.guardarAplicar", Left = 418, Top = 405, Width = 150, Height = 28 };
            btnGuardar.Click += (s, e) => GuardarAvanzados();
            pendientes.Add(btnGuardar);

            var sepFabrica = new Label { Left = margen, Top = 478, Width = anchoTotal, Height = 2, BorderStyle = BorderStyle.Fixed3D };

            var lblFabrica = new Label
            {
                Tag = "avz.fabricaTitulo", Left = margen, Top = 490, Width = anchoTotal,
                Font = new Font(Font.FontFamily, 9, FontStyle.Bold)
            };

            var notaFabrica = new Label
            {
                Tag = "avz.notaFabrica", Left = margen, Top = 514, Width = 380, Height = 34,
                ForeColor = Color.DimGray
            };

            var btnFabrica = new Button
            {
                Tag = "avz.restaurarFabrica", Left = 418, Top = 516, Width = 150, Height = 28,
                ForeColor = Color.Firebrick
            };
            btnFabrica.Click += (s, e) => RestaurarFabrica();
            pendientes.Add(btnFabrica);

            tab.Controls.AddRange(new Control[] {
                intro, lblCorriente, _numCorriente, notaCorriente, lblMicro, _cmbMicropasos, _chkInvertir,
                sep, lblStallTitulo, lblStall, _numStall, notaStall,
                lblMinVel, _numStallMinVel, notaMinVel, explicacion,
                lblHold, _numHoldCorriente, notaHold, btnGuardar,
                sepFabrica, lblFabrica, notaFabrica, btnFabrica
            });
        }

        private void GuardarAvanzados()
        {
            Proteger(() =>
            {
                if (_config == null) _config = LeerConfig();

                // El equipo arranca como enfocador o rotador según un jumper y
                // cada modo tiene su bloque, pero esto es un driver de
                // enfocador: en modo rotador no sería el adecuado.
                var dev = ObtenerSeccion(_config, SeccionDispositivo);
                dev["current_ma"] = (int)_numCorriente.Value;
                dev["microsteps"] = Convert.ToInt32(_cmbMicropasos.SelectedItem ?? (_rotador ? 8 : 4));
                dev["reverse"] = _chkInvertir.Checked;
                dev["stall_threshold"] = (int)_numStall.Value;
                dev["stall_min_speed"] = (int)_numStallMinVel.Value;
                dev["hold_current_ma"] = (int)_numHoldCorriente.Value;
                // El booleano viejo ya no lo interpreta nadie, y dejarlo en el
                // JSON solo daria pie a que alguien lo creyera vigente.
                dev.Remove("hold");

                GuardarConfig("msg.aplicadoEquipo");
            });
        }

        /// <summary>
        /// Borra la configuración del equipo y lo deja con los valores de
        /// fábrica. Se pide confirmación porque también se lleva por delante las
        /// redes WiFi guardadas, y tras el reinicio el equipo levanta su propio
        /// AP: hay que volver a configurarlas.
        /// </summary>
        private void RestaurarFabrica()
        {
            Proteger(() =>
            {
                var respuesta = MessageBox.Show(this, Textos.Get("avz.confirmarFabrica"), Text,
                                                MessageBoxButtons.YesNo, MessageBoxIcon.Warning,
                                                MessageBoxDefaultButton.Button2);
                if (respuesta != DialogResult.Yes) return;

                if (!_link.FactoryReset())
                {
                    MessageBox.Show(this, Textos.Get("msg.fabricaFallo"), Text,
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                // El equipo se reinicia y el puerto se cae: se cierra la
                // conexión igual que al guardar la WiFi, para que el usuario
                // vuelva a conectar cuando haya terminado de arrancar.
                _timer.Stop();
                _link.Close();
                _config = null;
                ActualizarHabilitacion();
                ActualizarTextoConectar();
                _lblEstado.Tag = null;
                _lblEstado.Text = Textos.Get("avz.reiniciandoFabrica");
                _lblEstado.ForeColor = Color.SeaGreen;
                _lblPosicion.Text = "—";

                MessageBox.Show(this, Textos.Get("msg.fabricaHecho"), Text,
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
            });
        }

        #endregion

        #region Pestaña WiFi

        private void ConstruirPestanaWifi(TabPage tab, List<Control> pendientes)
        {
            var lblIpTitulo = new Label { Tag = "wifi.ipActual", Left = 15, Top = 18, Width = 70 };
            _lblIp = new Label
            {
                Text = "—",
                Left = 85, Top = 16,
                // Hasta el botón de la derecha. Estrecho se partía en dos
                // líneas y la segunda quedaba cortada por el separador.
                Width = 335, Height = 20,
                AutoEllipsis = true,
                Font = new Font(Font.FontFamily, 10, FontStyle.Bold)
            };
            var btnIp = new Button { Tag = "wifi.leer", Left = 430, Top = 14, Width = 140 };
            btnIp.Click += (s, e) => RefrescarIp();
            pendientes.Add(btnIp);

            var sep = new Label { Left = 15, Top = 45, Width = 555, Height = 2, BorderStyle = BorderStyle.Fixed3D };
            var lblAp = new Label { Tag = "wifi.apTitulo", Left = 15, Top = 55, Width = 555, ForeColor = Color.DimGray };

            var lblApNombre = new Label { Tag = "wifi.nombre", Left = 15, Top = 80, Width = 70 };
            _txtApNombre = new TextBox { Left = 85, Top = 77, Width = 150 };
            var lblApPass = new Label { Tag = "wifi.password", Left = 245, Top = 80, Width = 80 };
            _txtApPass = new TextBox { Left = 330, Top = 77, Width = 120 };

            var lblTimeout = new Label { Tag = "wifi.timeout", Left = 15, Top = 112, Width = 90 };
            _numTimeout = new NumericUpDown { Left = 110, Top = 109, Width = 70, Minimum = 100, Maximum = 60000, Increment = 100 };
            var lblRetries = new Label { Tag = "wifi.reintentos", Left = 200, Top = 112, Width = 80 };
            _numRetries = new NumericUpDown { Left = 285, Top = 109, Width = 60, Minimum = 1, Maximum = 100 };
            // Apagado, el equipo arranca sin esperar al WiFi y solo se maneja
            // por USB. Lo guarda el mismo botón "Guardar y reiniciar".
            _chkWifi = new CheckBox { Tag = "wifi.activado", Left = 370, Top = 110, Width = 200, Checked = true };

            var lblRedes = new Label { Tag = "wifi.redes", Left = 15, Top = 145, Width = 555 };
            _gridRedes = new DataGridView
            {
                Left = 15, Top = 165, Width = 555, Height = 150,
                AllowUserToResizeRows = false,
                RowHeadersVisible = false,
                AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill,
                EditMode = DataGridViewEditMode.EditOnEnter
            };
            _gridRedes.Columns.Add("ssid", "SSID");
            _gridRedes.Columns.Add("password", "Password");

            var btnQuitar = new Button { Tag = "wifi.quitar", Left = 15, Top = 328, Width = 120 };
            btnQuitar.Click += (s, e) =>
            {
                if (_gridRedes.CurrentRow != null && !_gridRedes.CurrentRow.IsNewRow)
                    _gridRedes.Rows.Remove(_gridRedes.CurrentRow);
            };

            var btnRecargar = new Button { Tag = "wifi.recargar", Left = 143, Top = 328, Width = 160 };
            btnRecargar.Click += (s, e) => Proteger(() => { _config = LeerConfig(); VolcarConfigAControles(); });
            pendientes.Add(btnRecargar);

            var btnGuardar = new Button { Tag = "wifi.guardarReiniciar", Left = 400, Top = 328, Width = 170 };
            btnGuardar.Click += (s, e) => GuardarWifi();
            pendientes.Add(btnGuardar);

            var nota = new Label { Tag = "wifi.nota", Left = 15, Top = 368, Width = 555, Height = 75, ForeColor = Color.DimGray };

            tab.Controls.AddRange(new Control[] {
                lblIpTitulo, _lblIp, btnIp, sep, lblAp, lblApNombre, _txtApNombre, lblApPass, _txtApPass,
                lblTimeout, _numTimeout, lblRetries, _numRetries, _chkWifi, lblRedes, _gridRedes,
                btnQuitar, btnRecargar, btnGuardar, nota
            });
        }

        /// <summary>Las columnas de la rejilla no son controles, hay que traducirlas aparte.</summary>
        private void RenombrarColumnasRejilla()
        {
            if (_gridRedes == null || _gridRedes.Columns.Count < 2) return;
            _gridRedes.Columns[0].HeaderText = Textos.Get("wifi.colSsid");
            _gridRedes.Columns[1].HeaderText = Textos.Get("wifi.colPassword");
        }

        private void RefrescarIp()
        {
            Proteger(() =>
            {
                // Formato "AP:192.168.4.1" / "STA:192.168.1.50" / "OFF:0.0.0.0"
                string estado = _link.GetNetworkStatus();
                int sep = estado.IndexOf(':');
                if (sep <= 0) { _lblIp.Text = estado; return; }

                string modo = estado.Substring(0, sep);
                string ip = estado.Substring(sep + 1);
                if (modo == "STA") _lblIp.Text = string.Format(Textos.Get("wifi.modoSta"), ip);
                else if (modo == "AP") _lblIp.Text = string.Format(Textos.Get("wifi.modoAp"), ip);
                else _lblIp.Text = Textos.Get("wifi.sinRed");
            });
        }

        private void GuardarWifi()
        {
            Proteger(() =>
            {
                string pass = _txtApPass.Text;
                if (pass.Length > 0 && pass.Length < 8)
                {
                    MessageBox.Show(this, Textos.Get("wifi.passCorta"), Name,
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                if (_config == null) _config = LeerConfig();
                var wifi = ObtenerSeccion(_config, "wifi");

                wifi["ap_name"] = _txtApNombre.Text;
                wifi["ap_password"] = pass;
                wifi["timeout_ms"] = (int)_numTimeout.Value;
                wifi["retries"] = (int)_numRetries.Value;
                wifi["enabled"] = _chkWifi.Checked;

                var redes = new List<object>();
                foreach (DataGridViewRow fila in _gridRedes.Rows)
                {
                    if (fila.IsNewRow) continue;
                    string ssid = Convert.ToString(fila.Cells[0].Value ?? "");
                    if (string.IsNullOrEmpty(ssid.Trim())) continue;
                    redes.Add(new Dictionary<string, object> {
                        { "ssid", ssid },
                        { "password", Convert.ToString(fila.Cells[1].Value ?? "") }
                    });
                }
                wifi["networks"] = redes;

                if (!_link.SetConfigJson(_json.Serialize(_config)))
                {
                    MessageBox.Show(this, Textos.Get("msg.rechazado"), Text,
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                _link.Reboot();
                _timer.Stop();
                _link.Close();
                ActualizarHabilitacion();
                ActualizarTextoConectar();
                _lblEstado.Tag = null;
                _lblEstado.Text = Textos.Get("wifi.reiniciando");
                _lblEstado.ForeColor = Color.SeaGreen;
                _lblPosicion.Text = "—";
            });
        }

        #endregion

        #region Pestaña firmware

        // Lo que el equipo dice de sí mismo al conectar (:DV# y :DH#). Sin
        // :DH# el firmware es anterior a la actualización por USB.
        private Version _versionEquipo;
        private string _motorEquipo = "";
        private int _chipEquipo = -1;

        private TabControl _tabs;
        private TabPage _tabFirmware;
        // La pestaña se ilumina cuando hay un firmware más nuevo que el del
        // equipo para su motor, encontrado en internet o abierto en local.
        private bool _hayNovedad;
        private bool _grabando;

        private Label _lblFwEquipo;
        private Button _btnFwBuscar;
        private ListView _lvFw;
        private Label _lblFwInternet;
        private Button _btnFwAbrir;
        private Label _lblFwElegido;
        private Button _btnFwGrabar;
        private ProgressBar _pbFw;
        private Label _lblFwEstado;

        private List<FirmwareRemoto> _catalogo;
        private FirmwareRemoto _remotoElegido;
        private ImagenFirmware _imagenLocal;

        private void ConstruirPestanaFirmware(TabPage tab)
        {
            const int margen = 15;
            const int ancho = 560;

            _lblFwEquipo = new Label { Left = margen, Top = 15, Width = ancho, Height = 34, Font = new Font(Font, FontStyle.Bold) };

            var grpInternet = new GroupBox { Tag = "fw.internetTitulo", Left = margen - 5, Top = 55, Width = ancho + 10, Height = 255 };
            _btnFwBuscar = new Button { Tag = "fw.buscar", Left = 10, Top = 22, Width = 190, Height = 26 };
            _btnFwBuscar.Click += (s, e) => ConsultarCatalogo(true);
            _lblFwInternet = new Label { Left = 210, Top = 27, Width = 345, Height = 20, ForeColor = Color.DimGray };
            _lvFw = new ListView
            {
                Left = 10, Top = 56, Width = ancho - 10, Height = 188,
                View = View.Details, FullRowSelect = true, MultiSelect = false, HideSelection = false,
                // La descripción no cabe en su columna: entera en el tooltip.
                ShowItemToolTips = true
            };
            _lvFw.Columns.Add(new ColumnHeader { Tag = "fw.colVersion", Width = 70 });
            _lvFw.Columns.Add(new ColumnHeader { Tag = "fw.colMotor", Width = 80 });
            _lvFw.Columns.Add(new ColumnHeader { Tag = "fw.colFecha", Width = 85 });
            _lvFw.Columns.Add(new ColumnHeader { Tag = "fw.colDescripcion", Width = 300 });
            _lvFw.SelectedIndexChanged += (s, e) =>
            {
                if (_lvFw.SelectedItems.Count == 0) return;
                _remotoElegido = _lvFw.SelectedItems[0].Tag as FirmwareRemoto;
                _imagenLocal = null;
                ActualizarFirmwareElegido();
            };
            grpInternet.Controls.AddRange(new Control[] { _btnFwBuscar, _lblFwInternet, _lvFw });

            var grpLocal = new GroupBox { Tag = "fw.localTitulo", Left = margen - 5, Top = 320, Width = ancho + 10, Height = 80 };
            _btnFwAbrir = new Button { Tag = "fw.abrir", Left = 10, Top = 22, Width = 190, Height = 26 };
            _btnFwAbrir.Click += (s, e) => AbrirFirmwareLocal();
            var notaNombre = new Label { Tag = "fw.notaNombre", Left = 10, Top = 52, Width = ancho - 10, Height = 20, ForeColor = Color.DimGray };
            grpLocal.Controls.AddRange(new Control[] { _btnFwAbrir, notaNombre });

            _lblFwElegido = new Label { Left = margen, Top = 412, Width = ancho, Height = 36 };
            _btnFwGrabar = new Button { Tag = "fw.grabar", Left = margen, Top = 452, Width = 190, Height = 30 };
            _btnFwGrabar.Click += (s, e) => GrabarFirmware();
            _pbFw = new ProgressBar { Left = 215, Top = 456, Width = ancho - 200, Height = 22, Minimum = 0, Maximum = 100 };
            _lblFwEstado = new Label { Left = margen, Top = 490, Width = ancho, Height = 34 };
            var notaGrabar = new Label { Tag = "fw.notaGrabar", Left = margen, Top = 526, Width = ancho, Height = 32, ForeColor = Color.DimGray };

            tab.Controls.AddRange(new Control[] {
                _lblFwEquipo, grpInternet, grpLocal, _lblFwElegido, _btnFwGrabar, _pbFw, _lblFwEstado, notaGrabar
            });
            ActualizarInfoEquipoFirmware();
            ActualizarFirmwareElegido();
        }

        /// <summary>
        /// Dibuja las pestañas a mano solo para poder iluminar la de firmware:
        /// WinForms no deja colorear una pestaña suelta de otra forma.
        /// </summary>
        private void DibujarPestana(object sender, DrawItemEventArgs e)
        {
            TabPage pagina = _tabs.TabPages[e.Index];
            bool resaltada = pagina == _tabFirmware && _hayNovedad;
            bool elegida = (e.State & DrawItemState.Selected) != 0;

            Color fondo = resaltada ? Color.Gold : (elegida ? SystemColors.Window : SystemColors.Control);
            using (var pincel = new SolidBrush(fondo)) e.Graphics.FillRectangle(pincel, e.Bounds);

            using (var fuente = resaltada ? new Font(Font, FontStyle.Bold) : new Font(Font, FontStyle.Regular))
            {
                // Dibujadas a mano no se ven grises solas al deshabilitarlas.
                Color texto = _tabs.Enabled ? SystemColors.ControlText : SystemColors.GrayText;
                TextRenderer.DrawText(e.Graphics, pagina.Text, fuente, e.Bounds, texto,
                                      TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
        }

        private void MarcarNovedad(bool hay)
        {
            if (_hayNovedad == hay) return;
            _hayNovedad = hay;
            _tabs.Invalidate();
        }

        private static string NombreChip(int id)
        {
            switch (id)
            {
                case 5: return "ESP32-C3";
                case 9: return "ESP32-S3";
                default: return "id " + id;
            }
        }

        /// <summary>Lee de un equipo recién conectado lo que hace falta para actualizarlo.</summary>
        private void LeerIdentidadFirmware()
        {
            Version v;
            _versionEquipo = Version.TryParse(_versionFirmware, out v) ? v : null;
            if (!_link.GetHardware(out _motorEquipo, out _chipEquipo))
            {
                _motorEquipo = "";
                _chipEquipo = -1;
            }
        }

        private void OlvidarIdentidadFirmware()
        {
            _versionEquipo = null;
            _motorEquipo = "";
            _chipEquipo = -1;
            MarcarNovedad(false);
        }

        private bool EquipoActualizable { get { return _link.IsOpen && _chipEquipo >= 0; } }

        private void ActualizarInfoEquipoFirmware()
        {
            if (!_link.IsOpen)
            {
                _lblFwEquipo.Text = Textos.Get("fw.sinConexion");
                _lblFwEquipo.ForeColor = Color.DimGray;
            }
            else if (!EquipoActualizable)
            {
                _lblFwEquipo.Text = string.Format(Textos.Get("fw.sinSoporte"),
                                                  string.IsNullOrEmpty(_versionFirmware) ? "?" : _versionFirmware);
                _lblFwEquipo.ForeColor = Color.Firebrick;
            }
            else
            {
                _lblFwEquipo.Text = string.Format(Textos.Get("fw.equipo"), _versionFirmware, _motorEquipo, NombreChip(_chipEquipo));
                _lblFwEquipo.ForeColor = SystemColors.ControlText;
            }
        }

        /// <summary>
        /// Pide el catálogo a internet sin bloquear la ventana. Al conectar se
        /// hace solo y en silencio (si no hay red no se molesta al usuario);
        /// con el botón, los errores sí se enseñan.
        /// </summary>
        private void ConsultarCatalogo(bool aPeticion)
        {
            _btnFwBuscar.Enabled = false;
            _lblFwInternet.Text = Textos.Get("fw.buscando");
            _lblFwInternet.ForeColor = Color.DimGray;

            System.Threading.Tasks.Task.Factory.StartNew(() =>
            {
                List<FirmwareRemoto> lista = null;
                Exception error = null;
                try { lista = CatalogoFirmware.Consultar(); }
                catch (Exception ex) { error = ex; }

                if (IsDisposed || !IsHandleCreated) return;
                try { BeginInvoke((Action)(() => MostrarCatalogo(lista, error, aPeticion))); }
                catch (ObjectDisposedException) { }
            });
        }

        private void MostrarCatalogo(List<FirmwareRemoto> lista, Exception error, bool aPeticion)
        {
            _btnFwBuscar.Enabled = !_grabando;
            if (error != null)
            {
                _lblFwInternet.Text = aPeticion ? string.Format(Textos.Get("fw.errServidor"), error.Message) : "";
                _lblFwInternet.ForeColor = Color.Firebrick;
                return;
            }

            _catalogo = lista;
            RellenarListaFirmware();
            EvaluarNovedades();
        }

        private void RellenarListaFirmware()
        {
            _lvFw.Items.Clear();
            if (_catalogo == null) return;

            // Con el equipo identificado solo se ofrece lo de su motor: el de
            // otro motor arranca, pero con corrientes y pasos que no son los
            // suyos. Quien lo quiera a propósito puede abrirlo como fichero.
            bool filtrar = !string.IsNullOrEmpty(_motorEquipo);
            foreach (FirmwareRemoto f in _catalogo)
            {
                if (filtrar && !string.Equals(f.Motor, _motorEquipo, StringComparison.OrdinalIgnoreCase)) continue;
                var item = new ListViewItem(new[] { f.Version.ToString(), f.Motor, f.Fecha, f.Descripcion(Textos.Idioma) })
                {
                    Tag = f,
                    ToolTipText = f.Descripcion(Textos.Idioma)
                };
                _lvFw.Items.Add(item);
                if (f == _remotoElegido) item.Selected = true;
            }
            // Lo elegido antes de conectar puede ser de otro motor.
            if (_remotoElegido != null && _lvFw.SelectedItems.Count == 0)
            {
                _remotoElegido = null;
                ActualizarFirmwareElegido();
            }
        }

        /// <summary>
        /// ¿Hay algo más nuevo que lo del equipo para su motor? Mira el
        /// catálogo de internet y, si se abrió, el fichero local. Ilumina la
        /// pestaña y deja preseleccionado lo más nuevo de internet.
        /// </summary>
        private void EvaluarNovedades()
        {
            if (!EquipoActualizable)
            {
                MarcarNovedad(false);
                if (_catalogo != null)
                    _lblFwInternet.Text = string.Format(Textos.Get("fw.encontrados"), _catalogo.Count);
                return;
            }

            FirmwareRemoto nueva = _catalogo != null
                ? CatalogoFirmware.MasNuevaPara(_catalogo, _motorEquipo, _versionEquipo) : null;
            bool localNueva = _imagenLocal != null
                && string.Equals(_imagenLocal.Motor, _motorEquipo, StringComparison.OrdinalIgnoreCase)
                && (_versionEquipo == null || _imagenLocal.Version > _versionEquipo);

            if (_catalogo != null)
            {
                bool hayDelMotor = _catalogo.Exists(f => string.Equals(f.Motor, _motorEquipo, StringComparison.OrdinalIgnoreCase));
                if (nueva != null)
                {
                    _lblFwInternet.Text = string.Format(Textos.Get("fw.hayNueva"), nueva.Version);
                    _lblFwInternet.ForeColor = Color.DarkGoldenrod;
                    if (_remotoElegido == null && _imagenLocal == null)
                    {
                        foreach (ListViewItem item in _lvFw.Items)
                            if (item.Tag == nueva) { item.Selected = true; item.EnsureVisible(); }
                    }
                }
                else
                {
                    _lblFwInternet.Text = string.Format(Textos.Get(hayDelMotor ? "fw.alDia" : "fw.sinMotor"), _motorEquipo);
                    _lblFwInternet.ForeColor = hayDelMotor ? Color.SeaGreen : Color.DimGray;
                }
            }

            MarcarNovedad(nueva != null || localNueva);
        }

        private void AbrirFirmwareLocal()
        {
            using (var dlg = new OpenFileDialog
            {
                Filter = "AstroK firmware (astrok_*.bin)|astrok_*.bin|*.bin|*.bin",
                Title = Textos.Get("fw.abrir")
            })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                string claveError;
                ImagenFirmware img = null;
                try
                {
                    img = ImagenFirmware.Crear(System.IO.Path.GetFileName(dlg.FileName),
                                               System.IO.File.ReadAllBytes(dlg.FileName), out claveError);
                }
                catch (Exception ex) { claveError = null; MostrarEstadoFw(string.Format(Textos.Get("msg.error"), ex.Message), true); return; }

                if (img == null)
                {
                    MessageBox.Show(this, Textos.Get(claveError), Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                _imagenLocal = img;
                _remotoElegido = null;
                _lvFw.SelectedItems.Clear();
                ActualizarFirmwareElegido();
                EvaluarNovedades();
            }
        }

        private void ActualizarFirmwareElegido()
        {
            if (_imagenLocal != null)
                _lblFwElegido.Text = string.Format(Textos.Get("fw.elegido"), _imagenLocal.Nombre, _imagenLocal.Version,
                                                   _imagenLocal.Motor, _imagenLocal.Fecha.ToString("yyyy-MM-dd"));
            else if (_remotoElegido != null)
                _lblFwElegido.Text = string.Format(Textos.Get("fw.elegido"), _remotoElegido.Fichero, _remotoElegido.Version,
                                                   _remotoElegido.Motor, _remotoElegido.Fecha);
            else
                _lblFwElegido.Text = Textos.Get("fw.ningunoElegido");

            _btnFwGrabar.Enabled = EquipoActualizable && !_grabando && (_imagenLocal != null || _remotoElegido != null);
        }

        private void MostrarEstadoFw(string texto, bool error)
        {
            _lblFwEstado.Text = texto;
            _lblFwEstado.ForeColor = error ? Color.Firebrick : SystemColors.ControlText;
        }

        /// <summary>
        /// Descarga (si viene de internet), valida contra el equipo y graba.
        /// Las comprobaciones van de lo que no tiene arreglo (otro chip: se
        /// bloquea) a lo que puede ser a propósito (otro motor, misma versión o
        /// anterior: se pregunta).
        /// </summary>
        private void GrabarFirmware()
        {
            if (!EquipoActualizable) return;
            FirmwareRemoto remoto = _imagenLocal == null ? _remotoElegido : null;
            ImagenFirmware local = _imagenLocal;

            EmpezarGrabacion();
            if (remoto != null)
            {
                MostrarEstadoFw(string.Format(Textos.Get("fw.descargando"), remoto.Fichero), false);
                System.Threading.Tasks.Task.Factory.StartNew(() =>
                {
                    ImagenFirmware img = null;
                    string fallo = null;
                    try
                    {
                        string claveError;
                        img = ImagenFirmware.Crear(remoto.Fichero, CatalogoFirmware.Descargar(remoto), out claveError);
                        if (img == null) fallo = Textos.Get(claveError);
                    }
                    catch (Exception ex) { fallo = string.Format(Textos.Get("fw.errDescarga"), ex.Message); }

                    if (IsDisposed || !IsHandleCreated) return;
                    try
                    {
                        BeginInvoke((Action)(() =>
                        {
                            if (img == null) { TerminarGrabacion(fallo, true); return; }
                            ConfirmarYGrabar(img);
                        }));
                    }
                    catch (ObjectDisposedException) { }
                });
            }
            else
            {
                ConfirmarYGrabar(local);
            }
        }

        private void ConfirmarYGrabar(ImagenFirmware img)
        {
            if (img.ChipId != _chipEquipo)
            {
                TerminarGrabacion(string.Format(Textos.Get("fw.errChip"), NombreChip(img.ChipId), NombreChip(_chipEquipo)), true);
                return;
            }
            if (!string.Equals(img.Motor, _motorEquipo, StringComparison.OrdinalIgnoreCase)
                && !Preguntar(string.Format(Textos.Get("fw.confirmarMotor"), img.Motor, _motorEquipo), MessageBoxIcon.Warning))
            { TerminarGrabacion("", false); return; }
            if (_versionEquipo != null && img.Version <= _versionEquipo
                && !Preguntar(string.Format(Textos.Get("fw.confirmarVersion"), img.Version, _versionEquipo), MessageBoxIcon.Question))
            { TerminarGrabacion("", false); return; }
            if (!Preguntar(string.Format(Textos.Get("fw.confirmar"), img.Nombre), MessageBoxIcon.Question))
            { TerminarGrabacion("", false); return; }

            string puerto = ComPort;
            MostrarEstadoFw(string.Format(Textos.Get("fw.enviando"), 0), false);

            System.Threading.Tasks.Task.Factory.StartNew(() =>
            {
                string fallo = null;
                string nuevaVersion = null;
                string falloWeb = null;
                try
                {
                    int ultimo = -1;
                    _link.UpdateFirmware(img.Datos, (hecho, total) =>
                    {
                        int pct = (int)(100L * hecho / total);
                        if (pct == ultimo) return;
                        ultimo = pct;
                        try { BeginInvoke((Action)(() => { _pbFw.Value = pct; MostrarEstadoFw(string.Format(Textos.Get("fw.enviando"), pct), false); })); }
                        catch (ObjectDisposedException) { }
                    });
                    try { BeginInvoke((Action)(() => MostrarEstadoFw(Textos.Get("fw.reiniciando"), false))); }
                    catch (ObjectDisposedException) { }
                    _link.Close();
                    nuevaVersion = EsperarReinicio(puerto);
                }
                catch (Exception ex) { fallo = string.Format(Textos.Get("fw.error"), ex.Message); }

                // La web va después y con el firmware nuevo ya arrancado: es
                // el que entiende :UB...,W#. Si falla, el programa ya está
                // actualizado; se dice así para que no se repita a ciegas.
                if (fallo == null && img.Web != null && nuevaVersion != null)
                {
                    try
                    {
                        using (var web = new DeviceLink())
                        {
                            web.Open(puerto);
                            int ultimo = -1;
                            web.UpdateWeb(img.Web, (hecho, total) =>
                            {
                                int pct = (int)(100L * hecho / total);
                                if (pct == ultimo) return;
                                ultimo = pct;
                                try { BeginInvoke((Action)(() => { _pbFw.Value = pct; MostrarEstadoFw(string.Format(Textos.Get("fw.enviandoWeb"), pct), false); })); }
                                catch (ObjectDisposedException) { }
                            });
                        }
                        try { BeginInvoke((Action)(() => MostrarEstadoFw(Textos.Get("fw.reiniciando"), false))); }
                        catch (ObjectDisposedException) { }
                        nuevaVersion = EsperarReinicio(puerto) ?? nuevaVersion;
                    }
                    catch (Exception ex) { falloWeb = string.Format(Textos.Get("fw.errWeb"), nuevaVersion, ex.Message); }
                }

                if (IsDisposed || !IsHandleCreated) return;
                try
                {
                    BeginInvoke((Action)(() =>
                    {
                        if (fallo != null) { TerminarGrabacion(fallo, true); return; }
                        TerminarGrabacion("", false);
                        // Se reconecta como si el usuario pulsara Conectar: así
                        // se releen versión, motor y configuración de verdad.
                        AlternarConexion();
                        MostrarEstadoFw(nuevaVersion != null
                            ? string.Format(Textos.Get("fw.hecho"), nuevaVersion)
                            : Textos.Get("fw.hechoSinVersion"), nuevaVersion == null);
                        if (falloWeb != null) MostrarEstadoFw(falloWeb, true);
                    }));
                }
                catch (ObjectDisposedException) { }
            });
        }

        /// <summary>
        /// Tras :UE# el equipo se reinicia y el puerto USB desaparece un momento.
        /// Se espera a que vuelva y conteste a :DV#.
        /// </summary>
        private static string EsperarReinicio(string puerto)
        {
            System.Threading.Thread.Sleep(3000);
            DateTime limite = DateTime.Now.AddSeconds(40);
            while (DateTime.Now < limite)
            {
                try
                {
                    using (var prueba = new DeviceLink())
                    {
                        prueba.Open(puerto);
                        // El arranque del WiFi tarda; el firmware contesta mientras.
                        System.Threading.Thread.Sleep(1500);
                        string v = prueba.GetFirmwareVersion();
                        if (!string.IsNullOrEmpty(v)) return v;
                    }
                }
                catch { }
                System.Threading.Thread.Sleep(1000);
            }
            return null;
        }

        private bool Preguntar(string texto, MessageBoxIcon icono)
        {
            return MessageBox.Show(this, texto, Text, MessageBoxButtons.YesNo, icono, MessageBoxDefaultButton.Button2) == DialogResult.Yes;
        }

        private void EmpezarGrabacion()
        {
            _grabando = true;
            // Nada más puede hablar con el equipo mientras tanto.
            _timer.Stop();
            _pbFw.Value = 0;
            ActualizarHabilitacion();
        }

        private void TerminarGrabacion(string mensaje, bool error)
        {
            _grabando = false;
            if (_link.IsOpen) _timer.Start();
            ActualizarHabilitacion();
            if (!string.IsNullOrEmpty(mensaje)) MostrarEstadoFw(mensaje, error);
            else if (!error) MostrarEstadoFw("", false);
        }

        /// <summary>Textos que no se pueden poner por Tag: columnas y frases con datos.</summary>
        private void RetraducirFirmware()
        {
            foreach (ColumnHeader c in _lvFw.Columns) c.Text = Textos.Get(c.Tag as string);
            RellenarListaFirmware();
            ActualizarInfoEquipoFirmware();
            ActualizarFirmwareElegido();
            if (_catalogo != null) EvaluarNovedades();
        }

        #endregion

        /// <summary>Bloque del JSON que corresponde al producto en uso.</summary>
        private string SeccionDispositivo { get { return _rotador ? "rotator" : "focuser"; } }

        #region Conexión y configuración

        /// <summary>
        /// Busca el equipo: enumera los puertos USB y pregunta a cada uno quien
        /// es. Va en segundo plano porque abrir un puerto resetea el ESP32 y hay
        /// que esperar a que arranque, y con varios puertos eso son segundos.
        /// </summary>
        private void BuscarEquipos()
        {
            if (_buscando || _link.IsOpen) return;

            _buscando = true;
            ActualizarHabilitacion();
            _lblEstado.Tag = "foco.buscando";
            _lblEstado.Text = Textos.Get("foco.buscando");
            _lblEstado.ForeColor = Color.DimGray;

            System.Threading.Tasks.Task.Factory.StartNew(() =>
            {
                List<EquipoEncontrado> encontrados;
                try { encontrados = BuscadorEquipos.Buscar(_rotador); }
                catch { encontrados = new List<EquipoEncontrado>(); }

                // La ventana puede haberse cerrado mientras se sondeaba.
                if (IsDisposed || !IsHandleCreated) return;
                try { BeginInvoke((Action)(() => TerminarBusqueda(encontrados))); }
                catch (ObjectDisposedException) { }
            });
        }

        private void TerminarBusqueda(List<EquipoEncontrado> encontrados)
        {
            _buscando = false;
            _lblEstado.Tag = null; // texto con formato, no traducible por Tag

            _comBox.Items.Clear();
            foreach (EquipoEncontrado equipo in encontrados) _comBox.Items.Add(equipo);

            if (encontrados.Count > 0)
            {
                // Se respeta el puerto guardado si sigue estando entre los
                // encontrados; si no, el primero, que es el de COM mas bajo.
                int guardado = encontrados.FindIndex(
                    equipo => string.Equals(equipo.Puerto, _puertoPreferido, StringComparison.OrdinalIgnoreCase));
                _comBox.SelectedIndex = guardado >= 0 ? guardado : 0;

                _lblEstado.Text = string.Format(Textos.Get("foco.encontrados"), encontrados.Count);
                _lblEstado.ForeColor = Color.SeaGreen;
            }
            else
            {
                // La lista se queda vacia a proposito: no se ofrece ningun
                // puerto que no se haya confirmado como el producto de esta
                // aplicacion. El desplegable es editable, asi que queda la via
                // de escribir el puerto a mano.
                _comBox.Text = "";
                _lblEstado.Text = Textos.Get("foco.ningunEquipo");
                _lblEstado.ForeColor = Color.Firebrick;
            }

            ActualizarHabilitacion();
        }

        private void AlternarConexion()
        {
            if (_link.IsOpen)
            {
                _timer.Stop();
                _link.Close();
                _versionFirmware = "";
                ActualizarVersiones();
                OlvidarIdentidadFirmware();
                ActualizarInfoEquipoFirmware();
                ActualizarFirmwareElegido();
                _lblEstado.Tag = "foco.desconectado";
                _lblEstado.Text = Textos.Get("foco.desconectado");
                _lblEstado.ForeColor = Color.DimGray;
                _lblPosicion.Text = "—";
                ActualizarHabilitacion();
                ActualizarTextoConectar();
                return;
            }

            try
            {
                Cursor = Cursors.WaitCursor;
                _link.Open(ComPort);
                string version = _link.GetVersion();
                _versionFirmware = _link.GetFirmwareVersion();
                ActualizarVersiones();
                LeerIdentidadFirmware();

                _config = LeerConfig();
                VolcarConfigAControles();
                RefrescarIp();

                _lblEstado.Tag = null; // texto con formato, no traducible por Tag
                _lblEstado.Text = string.Format(Textos.Get("foco.conectadoA"), ComPort, version);
                _lblEstado.ForeColor = Color.SeaGreen;
                _timer.Start();

                // Con el motor ya conocido se mira si hay algo más nuevo en
                // internet, en segundo plano y sin avisar si no hay red.
                ActualizarInfoEquipoFirmware();
                RellenarListaFirmware();
                ActualizarFirmwareElegido();
                if (EquipoActualizable) ConsultarCatalogo(false);
            }
            catch (Exception ex)
            {
                _link.Close();
                _lblEstado.Tag = null;
                _lblEstado.Text = string.Format(Textos.Get("foco.noConecta"), ex.Message);
                _lblEstado.ForeColor = Color.Firebrick;
            }
            finally
            {
                Cursor = Cursors.Default;
                ActualizarHabilitacion();
                ActualizarTextoConectar();
            }
        }

        private void ActualizarTextoConectar()
        {
            _btnConectar.Text = Textos.Get(_link.IsOpen ? "foco.desconectar" : "foco.conectar");
        }

        /// <summary>Escribe la configuración en el equipo e informa del resultado.</summary>
        private void GuardarConfig(string claveExito)
        {
            if (_link.SetConfigJson(_json.Serialize(_config)))
                MessageBox.Show(this, Textos.Get(claveExito), Text,
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
            else
                MessageBox.Show(this, Textos.Get("msg.rechazado"), Text,
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private Dictionary<string, object> LeerConfig()
        {
            string json = _link.GetConfigJson();
            return _json.Deserialize<Dictionary<string, object>>(json) ?? new Dictionary<string, object>();
        }

        private static Dictionary<string, object> ObtenerSeccion(Dictionary<string, object> raiz, string nombre)
        {
            object seccion;
            if (raiz.TryGetValue(nombre, out seccion) && seccion is Dictionary<string, object>)
                return (Dictionary<string, object>)seccion;

            var nueva = new Dictionary<string, object>();
            raiz[nombre] = nueva;
            return nueva;
        }

        private void VolcarConfigAControles()
        {
            var wifi = ObtenerSeccion(_config, "wifi");
            _txtApNombre.Text = Convert.ToString(Valor(wifi, "ap_name", ""));
            _txtApPass.Text = Convert.ToString(Valor(wifi, "ap_password", ""));
            _numTimeout.Value = Acotar(_numTimeout, Convert.ToInt32(Valor(wifi, "timeout_ms", 1000)));
            _numRetries.Value = Acotar(_numRetries, Convert.ToInt32(Valor(wifi, "retries", 10)));
            // Sin la clave (firmware anterior a 1.4.3) el WiFi está encendido.
            _chkWifi.Checked = Convert.ToBoolean(Valor(wifi, "enabled", true));

            _gridRedes.Rows.Clear();
            var redes = Valor(wifi, "networks", null) as ArrayList;
            if (redes != null)
            {
                foreach (object item in redes)
                {
                    var red = item as Dictionary<string, object>;
                    if (red == null) continue;
                    _gridRedes.Rows.Add(
                        Convert.ToString(Valor(red, "ssid", "")),
                        Convert.ToString(Valor(red, "password", "")));
                }
            }

            var dev = ObtenerSeccion(_config, SeccionDispositivo);
            if (_rotador)
            {
                _pasosPorGrado = Convert.ToDouble(Valor(dev, "steps_per_degree", 37.75));
                if (_pasosPorGrado <= 0) _pasosPorGrado = 37.75;
                _numMax.Value = (decimal)_pasosPorGrado;
            }
            else
            {
                _numMax.Value = Acotar(_numMax, Convert.ToInt32(Valor(dev, "max_steps", DeviceLink.MaxPasosEnfocador)));
            }
            _numCorriente.Value = Acotar(_numCorriente, Convert.ToInt32(Valor(dev, "current_ma", 800)));
            _numStall.Value = Acotar(_numStall, Convert.ToInt32(Valor(dev, "stall_threshold", _rotador ? 6 : 100)));
            _numStallMinVel.Value = Acotar(_numStallMinVel, Convert.ToInt32(Valor(dev, "stall_min_speed", 400)));
            // Equipo con firmware anterior: no tiene la clave en mA, solo el
            // booleano, y entonces el driver dejaba la mitad de la de marcha.
            int corrienteMarcha = Convert.ToInt32(Valor(dev, "current_ma", 400));
            object holdMa = Valor(dev, "hold_current_ma", null);
            int hold = holdMa != null
                ? Convert.ToInt32(holdMa)
                : (Convert.ToBoolean(Valor(dev, "hold", true)) ? corrienteMarcha / 2 : 0);
            _numHoldCorriente.Value = Acotar(_numHoldCorriente, hold);

            int micropasos = Convert.ToInt32(Valor(dev, "microsteps", _rotador ? 8 : 4));
            if (!_cmbMicropasos.Items.Contains(micropasos)) _cmbMicropasos.Items.Add(micropasos);
            _cmbMicropasos.SelectedItem = micropasos;

            _chkInvertir.Checked = Convert.ToBoolean(Valor(dev, "reverse", false));
        }

        private static object Valor(Dictionary<string, object> d, string clave, object porDefecto)
        {
            object v;
            return (d.TryGetValue(clave, out v) && v != null) ? v : porDefecto;
        }

        private static decimal Acotar(NumericUpDown control, int valor)
        {
            if (valor < control.Minimum) return control.Minimum;
            if (valor > control.Maximum) return control.Maximum;
            return valor;
        }

        /// <summary>Ejecuta una acción sobre el equipo informando del fallo sin cerrar la ventana.</summary>
        private void Proteger(Action accion)
        {
            if (!_link.IsOpen)
            {
                MessageBox.Show(this, Textos.Get("msg.conectaPrimero"), Text,
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            try { accion(); }
            catch (Exception ex)
            {
                _lblEstado.Tag = null;
                _lblEstado.Text = string.Format(Textos.Get("msg.error"), ex.Message);
                _lblEstado.ForeColor = Color.Firebrick;
            }
        }

        private void ActualizarHabilitacion()
        {
            // Durante una actualización de firmware todo queda bloqueado:
            // cualquier otro comando al equipo se mezclaría con la transferencia.
            bool conectado = _link.IsOpen && !_grabando;
            foreach (Control c in _controlesQuePidenConexion) c.Enabled = conectado;
            _gridRedes.Enabled = conectado;
            _comBox.Enabled = !_link.IsOpen && !_buscando && !_grabando;
            _btnBuscar.Enabled = !_link.IsOpen && !_buscando && !_grabando;
            _btnConectar.Enabled = !_buscando && !_grabando;

            // Sin equipo, pestañas deshabilitadas y vacías: lo que hubiera en
            // los controles es de la última conexión (o los valores por
            // defecto) y no del equipo que se vaya a conectar. Al conectar se
            // rellenan con lo leído antes de volver a enseñarse. Durante una
            // actualización el puerto se cierra a ratos (reinicios), pero la
            // pestaña de firmware tiene que seguir a la vista.
            bool hayEquipo = _link.IsOpen || _grabando;
            if (_tabs.Enabled != hayEquipo)
            {
                _tabs.Enabled = hayEquipo;
                foreach (TabPage p in _tabs.TabPages)
                    foreach (Control c in p.Controls) c.Visible = hayEquipo;
                _tabs.Invalidate();
            }

            if (_btnFwAbrir != null)
            {
                _btnFwAbrir.Enabled = !_grabando;
                _btnFwBuscar.Enabled = !_grabando;
                _lvFw.Enabled = !_grabando;
                ActualizarFirmwareElegido();
            }
        }

        #endregion

        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            RenombrarColumnasRejilla();
            ActualizarTextoConectar();
        }

        /// <summary>
        /// La busqueda arranca sola al abrir, no al pulsar: con el puerto ya
        /// identificado lo normal es que solo haya que darle a Conectar. Va
        /// aqui y no en OnLoad para que la ventana este a la vista mientras
        /// sondea, que tarda un par de segundos.
        /// </summary>
        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            BuscarEquipos();
            ComprobarAppNueva();
        }
    }
}
