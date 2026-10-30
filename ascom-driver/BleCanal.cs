using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Foundation;

namespace ASCOM.AstroK
{
    /// <summary>
    /// Falta el PIN de Bluetooth del equipo, o el guardado ya no vale. Lo
    /// pide la aplicación AstroK; el driver ASCOM solo usa el que haya
    /// guardado.
    /// </summary>
    public class BlePinException : IOException
    {
        /// <summary>El equipo ya tiene PIN (si no, se elige uno nuevo).</summary>
        public bool PinDefinido { get; private set; }
        /// <summary>Se probó un PIN guardado y el equipo lo rechazó.</summary>
        public bool Incorrecto { get; private set; }

        public BlePinException(bool pinDefinido, bool incorrecto)
            : base(incorrecto
                ? "Wrong Bluetooth PIN. Connect from the AstroK app to enter it again."
                : "Bluetooth PIN needed. Connect once from the AstroK app to enter it.")
        {
            PinDefinido = pinDefinido;
            Incorrecto = incorrecto;
        }
    }

    /// <summary>
    /// PIN de Bluetooth de cada equipo, por dirección, en el registro del
    /// usuario: lo comparten la aplicación y el driver ASCOM, que así no lo
    /// vuelve a pedir.
    /// </summary>
    internal static class PinesBle
    {
        private const string Clave = @"Software\AstroK\Bluetooth";

        private static string Nombre(ulong direccion)
        {
            return direccion.ToString("X12", CultureInfo.InvariantCulture);
        }

        public static string Leer(ulong direccion)
        {
            using (var k = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(Clave))
                return k == null ? null : k.GetValue(Nombre(direccion)) as string;
        }

        public static void Guardar(ulong direccion, string pin)
        {
            using (var k = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(Clave))
                k.SetValue(Nombre(direccion), pin);
        }

        public static void Borrar(ulong direccion)
        {
            using (var k = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(Clave, true))
                if (k != null) k.DeleteValue(Nombre(direccion), false);
        }

        public static bool Valido(string pin)
        {
            if (pin == null || pin.Length != 4) return false;
            foreach (char c in pin) if (c < '0' || c > '9') return false;
            return true;
        }
    }

    /// <summary>
    /// Canal del protocolo serie sobre BLE (servicio Nordic UART del
    /// firmware, ver BleLink.h): lo que se escribe va a RX y las respuestas
    /// llegan como notificaciones de TX. Imita lo que DeviceLink usa de
    /// SerialPort, así el resto del código no distingue un canal del otro.
    ///
    /// Sin emparejamiento de Windows: al abrir se da el PIN de 4 cifras del
    /// equipo (:BA#), el guardado en PinesBle. Sin él, o si el equipo lo
    /// rechaza, lanza BlePinException. Un equipo sin PIN se queda con el
    /// primero que recibe.
    ///
    /// Usa las APIs WinRT de Windows 10 directamente desde .NET Framework
    /// (Windows.winmd), sin paquetes NuGet.
    /// </summary>
    internal sealed class BleCanal : DeviceLink.ICanal
    {
        public const string Prefijo = "BLE:";

        private static readonly Guid Servicio = new Guid("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
        private static readonly Guid Rx = new Guid("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
        private static readonly Guid Tx = new Guid("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

        // Datos de fabricante del anuncio: 0xFFFF (reservado para pruebas)
        // seguido de 'A' 'K' y el modo, 'F' o 'R'.
        private const ushort Fabricante = 0xFFFF;

        private const int TimeoutConexionMs = 15000;
        private const int Intentos = 3;

        private BluetoothLEDevice _dispositivo;
        private GattDeviceService _servicio;
        private GattCharacteristic _rx;
        private GattCharacteristic _tx;
        private GattSession _sesion;
        private int _trozo = 20;
        private bool _abierto;
        // Se activa para que el hilo de Reservar() suelte el equipo.
        private ManualResetEvent _soltar;

        private readonly StringBuilder _recibido = new StringBuilder();
        private readonly object _lockRx = new object();

        public int ReadTimeout { get; set; }
        public bool IsOpen { get { return _abierto; } }

        /// <summary>"BLE:7CE8B1D38A1A" -&gt; dirección. Lanza si no lo es.</summary>
        public static ulong Direccion(string puerto)
        {
            string hex = puerto.Substring(Prefijo.Length).Replace(":", "");
            return ulong.Parse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        }

        public static string Puerto(ulong direccion)
        {
            return Prefijo + direccion.ToString("X12", CultureInfo.InvariantCulture);
        }

        public static bool EsBle(string puerto)
        {
            return puerto != null && puerto.StartsWith(Prefijo, StringComparison.OrdinalIgnoreCase);
        }

        public BleCanal(string puerto)
        {
            ReadTimeout = 3000;
            ulong direccion = Direccion(puerto);
            try
            {
                Reservar(direccion);
                // Reconectar justo después de cerrar falla a veces (Windows
                // aún está soltando la conexión anterior y cancela la
                // suscripción): un par de reintentos lo cubren.
                for (int intento = 1; ; intento++)
                {
                    try { Abrir(direccion); break; }
                    catch (Exception) when (intento < Intentos)
                    {
                        SoltarConexion();
                        Thread.Sleep(1000);
                    }
                }
                Autenticar(direccion);
            }
            catch { Close(); throw; }
        }

        /// <summary>Da el PIN guardado; sin él, o si no vale, BlePinException.</summary>
        private void Autenticar(ulong direccion)
        {
            bool definido = Pedir(":BQ#") == "1";
            string pin = PinesBle.Leer(direccion);
            if (!PinesBle.Valido(pin)) throw new BlePinException(definido, false);
            if (Pedir(":BA" + pin + "#") == "OK") return;
            // No vale (el PIN se cambió o se borró por USB): se olvida para
            // que la aplicación lo pida otra vez.
            PinesBle.Borrar(direccion);
            throw new BlePinException(definido, true);
        }

        private string Pedir(string comando)
        {
            DiscardInBuffer();
            Write(comando);
            return ReadTo("#");
        }

        private void Abrir(ulong direccion)
        {
            _dispositivo = Esperar(BluetoothLEDevice.FromBluetoothAddressAsync(direccion), TimeoutConexionMs);
            if (_dispositivo == null) throw new IOException("Bluetooth device not found");
            // Emparejado por la 2.1.0 (firmware 1.5.0, que emparejaba): ese
            // vínculo ya no existe en el equipo y Windows intentaría cifrar con
            // él. Se deshace.
            if (_dispositivo.DeviceInformation.Pairing.IsPaired)
            {
                Esperar(_dispositivo.DeviceInformation.Pairing.UnpairAsync(), TimeoutConexionMs);
                _dispositivo.Dispose();
                _dispositivo = Esperar(BluetoothLEDevice.FromBluetoothAddressAsync(direccion), TimeoutConexionMs);
                if (_dispositivo == null) throw new IOException("Bluetooth device not found");
            }

            // Que Windows no suelte la conexión entre comando y comando: sin
            // esto la cierra al poco de quedarse sin tráfico, y el siguiente
            // comando paga la reconexión.
            _sesion = Esperar(GattSession.FromDeviceIdAsync(_dispositivo.BluetoothDeviceId), TimeoutConexionMs);
            _sesion.MaintainConnection = true;
            DateTime limite = DateTime.UtcNow.AddMilliseconds(TimeoutConexionMs);
            while (_sesion.SessionStatus != GattSessionStatus.Active)
            {
                if (DateTime.UtcNow > limite) throw new IOException("Bluetooth connection not established");
                Thread.Sleep(50);
            }

            // Con la caché de Windows, no Uncached: releer los servicios del
            // equipo llegó a fallar ("el dispositivo no reconoce el comando").
            // La primera vez la caché está vacía y Windows los lee igualmente;
            // y la tabla del firmware no cambia.
            GattDeviceServicesResult servicios = Esperar(
                _dispositivo.GetGattServicesForUuidAsync(Servicio, BluetoothCacheMode.Cached), TimeoutConexionMs);
            if (servicios.Status != GattCommunicationStatus.Success || servicios.Services.Count == 0)
                throw new IOException("AstroK Bluetooth service not reachable (" + servicios.Status + ")");
            _servicio = servicios.Services[0];

            _rx = Caracteristica(Rx);
            _tx = Caracteristica(Tx);

            _tx.ValueChanged += AlRecibir;
            GattCommunicationStatus st = Esperar(
                _tx.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue.Notify), TimeoutConexionMs);
            if (st != GattCommunicationStatus.Success)
                throw new IOException("Cannot subscribe to the device (" + st + ")");

            // 3 bytes de cabecera ATT por paquete.
            _trozo = Math.Max(20, _sesion.MaxPduSize - 3);
            _abierto = true;
        }

        /// <summary>
        /// Un solo programa por equipo. Windows comparte la conexión BLE entre
        /// procesos: con dos abiertos (driver en NINA y la aplicación, por
        /// ejemplo) a cada uno le llegarían también las respuestas del otro.
        /// Por USB lo evita la exclusividad del puerto COM; aquí, un mutex con
        /// nombre por dirección.
        ///
        /// Lo retiene un hilo propio porque un Mutex solo lo puede soltar el
        /// hilo que lo tomó, y Open y Close pueden llegar de hilos distintos.
        /// Si el programa muere sin cerrar, Windows abandona el mutex y el
        /// siguiente lo toma igualmente.
        /// </summary>
        private void Reservar(ulong direccion)
        {
            string nombre = "AstroK_BLE_" + direccion.ToString("X12", CultureInfo.InvariantCulture);
            var soltar = new ManualResetEvent(false);
            bool conseguido = false;

            using (var listo = new ManualResetEvent(false))
            {
                var guardian = new Thread(() =>
                {
                    using (var m = new Mutex(false, nombre))
                    {
                        try { conseguido = m.WaitOne(0); }
                        catch (AbandonedMutexException) { conseguido = true; }
                        listo.Set();
                        if (!conseguido) return;
                        soltar.WaitOne();
                        m.ReleaseMutex();
                    }
                    soltar.Dispose();
                }) { IsBackground = true, Name = "AstroK BLE " + nombre };
                guardian.Start();
                listo.WaitOne();
            }

            if (!conseguido)
            {
                soltar.Dispose();
                throw new IOException("The device is already connected over Bluetooth from another program on this PC. Disconnect it there first.");
            }
            _soltar = soltar;
        }

        private GattCharacteristic Caracteristica(Guid uuid)
        {
            GattCharacteristicsResult r = Esperar(
                _servicio.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Cached), TimeoutConexionMs);
            if (r.Status != GattCommunicationStatus.Success || r.Characteristics.Count == 0)
                throw new IOException("AstroK Bluetooth characteristic missing (" + r.Status + ")");
            return r.Characteristics[0];
        }

        private void AlRecibir(GattCharacteristic c, GattValueChangedEventArgs args)
        {
            byte[] datos = args.CharacteristicValue.ToArray();
            lock (_lockRx)
            {
                _recibido.Append(Encoding.ASCII.GetString(datos));
                Monitor.PulseAll(_lockRx);
            }
        }

        public void Write(string texto)
        {
            if (!_abierto) throw new InvalidOperationException("Port closed");

            byte[] datos = Encoding.ASCII.GetBytes(texto);
            for (int hecho = 0; hecho < datos.Length; hecho += _trozo)
            {
                int n = Math.Min(_trozo, datos.Length - hecho);
                byte[] trozo = new byte[n];
                Array.Copy(datos, hecho, trozo, 0, n);
                // Sin confirmación: con ella cada paquete esperaría su propio
                // viaje de ida y vuelta, y un trozo de firmware son seis.
                GattWriteResult r = Esperar(
                    _rx.WriteValueWithResultAsync(trozo.AsBuffer(), GattWriteOption.WriteWithoutResponse), ReadTimeout);
                if (r.Status != GattCommunicationStatus.Success)
                    throw new IOException("Bluetooth write failed (" + r.Status + ")");
            }
        }

        public string ReadTo(string fin)
        {
            DateTime limite = DateTime.UtcNow.AddMilliseconds(ReadTimeout);
            lock (_lockRx)
            {
                while (true)
                {
                    int i = _recibido.ToString().IndexOf(fin, StringComparison.Ordinal);
                    if (i >= 0)
                    {
                        string respuesta = _recibido.ToString(0, i);
                        _recibido.Remove(0, i + fin.Length);
                        return respuesta;
                    }

                    int queda = (int)(limite - DateTime.UtcNow).TotalMilliseconds;
                    if (queda <= 0 || !_abierto) throw new TimeoutException("No answer from the device over Bluetooth");
                    Monitor.Wait(_lockRx, queda);
                }
            }
        }

        public void DiscardInBuffer()
        {
            lock (_lockRx) _recibido.Clear();
        }

        public void Close()
        {
            _abierto = false;
            lock (_lockRx) Monitor.PulseAll(_lockRx);
            SoltarConexion();

            // Después de soltar la conexión, para que el siguiente programa
            // no se cruce con los restos de esta.
            if (_soltar != null) { _soltar.Set(); _soltar = null; }
        }

        private void SoltarConexion()
        {
            // Windows mantiene la conexión mientras quede vivo algún objeto
            // del dispositivo: hay que soltarlos todos para que el equipo
            // vuelva a anunciarse y lo vean otros programas.
            if (_tx != null) { try { _tx.ValueChanged -= AlRecibir; } catch { } _tx = null; }
            _rx = null;
            if (_servicio != null) { try { _servicio.Dispose(); } catch { } _servicio = null; }
            if (_sesion != null) { try { _sesion.MaintainConnection = false; _sesion.Dispose(); } catch { } _sesion = null; }
            if (_dispositivo != null) { try { _dispositivo.Dispose(); } catch { } _dispositivo = null; }
        }

        // ---- Búsqueda ----

        /// <summary>
        /// Escucha anuncios BLE durante un rato y devuelve los AstroK del modo
        /// pedido. El modo viaja en los datos de fabricante, así que no hace
        /// falta conectar para filtrar. Un equipo que ya
        /// tenga conectado otro programa no se anuncia y no aparece.
        /// </summary>
        public static List<EquipoEncontrado> Buscar(bool rotador, int duracionMs)
        {
            char modoBuscado = rotador ? 'R' : 'F';
            var vistos = new Dictionary<ulong, EquipoEncontrado>();

            var escucha = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
            escucha.Received += (s, a) =>
            {
                char modo = '\0';
                foreach (BluetoothLEManufacturerData m in a.Advertisement.ManufacturerData)
                {
                    if (m.CompanyId != Fabricante || m.Data.Length < 3) continue;
                    byte[] d = m.Data.ToArray();
                    if (d[0] == 'A' && d[1] == 'K') modo = (char)d[2];
                }

                lock (vistos)
                {
                    EquipoEncontrado e;
                    if (!vistos.TryGetValue(a.BluetoothAddress, out e))
                    {
                        // El nombre llega en la respuesta al escaneo, que es
                        // otro evento: se recuerda la dirección aunque aún no
                        // se sepa si es un AstroK.
                        e = new EquipoEncontrado { Puerto = Puerto(a.BluetoothAddress), Firmware = "", Bluetooth = true };
                        vistos[a.BluetoothAddress] = e;
                    }
                    if (modo != '\0') e.Modo = modo == 'R' ? "ROTATOR" : "FOCUSER";
                    if (!string.IsNullOrEmpty(a.Advertisement.LocalName)) e.Nombre = a.Advertisement.LocalName;
                }
            };

            try
            {
                escucha.Start();
                Thread.Sleep(duracionMs);
            }
            catch
            {
                // Sin adaptador Bluetooth, o apagado: no hay nada que buscar.
            }
            finally
            {
                try { escucha.Stop(); } catch { }
            }

            var resultado = new List<EquipoEncontrado>();
            lock (vistos)
            {
                foreach (EquipoEncontrado e in vistos.Values)
                    if (e.Modo == (modoBuscado == 'R' ? "ROTATOR" : "FOCUSER")) resultado.Add(e);
            }
            return resultado;
        }

        // ---- Utilidades ----

        private static T Esperar<T>(IAsyncOperation<T> operacion, int timeoutMs)
        {
            Task<T> t = operacion.AsTask();
            try
            {
                if (!t.Wait(timeoutMs)) throw new TimeoutException("Bluetooth operation timed out");
            }
            catch (AggregateException ex) { throw ex.InnerException; }
            return t.Result;
        }
    }
}
