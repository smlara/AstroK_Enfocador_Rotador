# Driver ASCOM para AstroK (enfocador, USB)

Driver `IFocuserV3` que habla el protocolo serie propio del firmware.

**Estado: compilado y probado contra la placa real.** Conecta, lee posición, mueve el motor con
su rampa de aceleración, reporta `IsMoving` y para con `Halt()`.

## Por qué existe

El firmware del dispositivo responde correctamente por serie:
`:GV#`→`10#`, `:GP#`/`:GN#`/`:GI#` correctos, y `:SN####`+`:FG#` mueve el motor. Ningún driver
genérico de terceros sirve: el protocolo no es compatible con ninguno, y además la variante
ESP32‑C3 tiene un problema propio con el USB:

El C3 no tiene USB‑OTG; usa el periférico **USB Serial/JTAG**, que solo entrega datos cuando el
host aserta **DTR**. Medido sobre la placa, con las cuatro combinaciones:

| DTR | RTS | Resultado |
|---|---|---|
| False | False | silencio |
| False | True | silencio |
| True | False | silencio |
| **True** | **True** | **responde** |

Los drivers ASCOM se construyen sobre `System.IO.Ports.SerialPort`, cuyos valores por defecto
son `DtrEnable = false` y `RtsEnable = false`. Abren el puerto, el CDC no entrega nada, timeout.
Es sistemático, no mala suerte.

Este driver lo resuelve con dos líneas, en `DeviceLink.cs` → `Open()`:

```csharp
DtrEnable = true,
RtsEnable = true
```

Todo lo demás es la implementación estándar de `IFocuserV3`.

## Dos formas de usarlo

El instalador deja dos cosas:

1. **El driver ASCOM**, para N.I.N.A. y cualquier otro programa de captura.
2. **AstroK - Configuración del enfocador**, una aplicación independiente (menú Inicio) que
   abre esa misma ventana de configuración **sin pasar por N.I.N.A. ni por el Chooser de ASCOM**.
   Útil para dejar el equipo configurado en un portátil limpio, o para tocar la WiFi sin abrir el
   programa de captura.

La aplicación independiente no toca el perfil ASCOM: todo lo que hace va contra el equipo por
USB. De hecho `DeviceLink` y la ventana no referencian la plataforma ASCOM en absoluto.

## Idiomas

Español e inglés, con selector en la parte inferior de la ventana. Arranca en el idioma de
Windows si está disponible, y si no en español.

Los textos viven en `locales\es.json` y `locales\en.json`, **embebidos en el propio DLL**: así
solo hay un archivo que instalar, sin subcarpetas de ensamblados satélite. Cada control lleva su
clave en la propiedad `Tag` y `Textos.Aplicar()` recorre el árbol asignando el texto, que es lo
que permite cambiar de idioma en caliente sin reconstruir la ventana. Si falta una traducción se
muestra `[clave]` en pantalla, para que se note en vez de quedar el control en blanco.

Para añadir un idioma: copia uno de los JSON, tradúcelo, añádelo como `EmbeddedResource` en el
`.csproj` y mete su código en `Disponibles` de `Textos.cs`.

## Diálogo de configuración

La ventana tiene tres pestañas y habla con el equipo por su propia conexión serie (desde el
driver se abre con el botón del engranaje, con el driver desconectado):

**Enfocador** — puerto COM, conectar/desconectar, posición en vivo, saltos de ±10/±100/±500
(los mismos del encoder físico), parada, mover a una posición concreta, fijar la posición actual
a un valor (*sync*, para recuperar la referencia si se perdió el cero) y la posición máxima, que
se guarda **en el equipo**, no solo en el perfil ASCOM.

**Avanzados** — corriente del motor, micropasos, sensibilidad y velocidad mínima de StallGuard,
y el par en reposo. Se aplican **en caliente** al guardar, sin reiniciar: ajustar la sensibilidad
a base de reinicios entre prueba y prueba no es practicable.

**WiFi** — IP actual, nombre y contraseña del punto de acceso propio, timeout y reintentos, y la
lista de redes conocidas. Al guardar se reinicia el equipo, porque la configuración de red solo
se aplica al arrancar.

Esta segunda pestaña es la razón de ser de las extensiones del protocolo: **la configuración de
red solo era accesible por HTTP, y eso no sirve justo cuando la WiFi es lo que hay que
arreglar**. Ahora se puede dejar la red configurada por cable.

## Instalación

Doble clic en **`bin\AstroKFocuser-Setup-<versión>.exe`** (el `bin\` de la raíz del repositorio) (o el `AstroKRotator-…` del
rotador). Pide elevación (UAC), instala y registra el driver, y queda en *Agregar o quitar
programas* para desinstalarlo.

El instalador está en español e inglés (Inno ofrece elegir al arrancar). Sus textos propios
viven en `[CustomMessages]` de cada `.iss`, que comparten la misma estructura.

Lo que lee el usuario final no es este README sino `README-<producto>_es.txt` y
`README-<producto>_en.txt`, que el instalador copia como `README_es.txt` y `README_en.txt`; al
terminar ofrece abrir el del idioma elegido. Van en texto plano (UTF-8 con BOM y CRLF, para el
Bloc de notas) porque Windows no tiene programa asociado a `.md`.

Si no existe ese `.exe`, genéralo con `build.ps1` (ver más abajo).

Después, en N.I.N.A.: **Equipment → Focuser → ASCOM → "AstroK Focuser"**, botón del
engranaje para fijar el puerto COM y la posición máxima, y conectar.

Qué hace el instalador, por si quieres saberlo:

- Copia la DLL a `C:\Program Files (x86)\ASCOM\AstroK\`. La ubicación importa porque el
  registro se hace con `/codebase`, que graba la **ruta absoluta**: si luego mueves la DLL, se
  rompe el registro.
- La registra con **las dos versiones de RegAsm (32 y 64 bits)**. Hace falta: el Chooser de
  ASCOM es una aplicación de 32 bits y N.I.N.A. es de 64, y cada una mira su propia vista del
  registro.
- `RegAsm` dispara `RegisterASCOM()` del driver, que es lo que crea la entrada en el perfil
  ASCOM. El script lo verifica al terminar.

## Generar el instalador

Un solo comando, desde esta carpeta:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

Compila el driver con MSBuild (que localiza con `vswhere`, sin rutas cableadas), lanza Inno
Setup y deja el `.exe` en **`bin\`** de la raíz, junto a los firmwares: es la misma carpeta
que se sube al servidor con `fw.php`.

Lo que hizo falta en esta máquina (por si montas otra):

| Componente | Estado |
|---|---|
| ASCOM Platform + **Platform 6 Developer Components** | ya instalado |
| Visual Studio Community 2022 (aporta MSBuild) | ya instalado |
| Inno Setup 6 (genera el `.exe`) | ya instalado, en `%LOCALAPPDATA%\Programs` |
| **.NET Framework 4.8.1 Developer Pack** | **faltaba** |

VS 2022 estaba sin la carga de trabajo de escritorio .NET, así que no había ningún *targeting
pack* de .NET Framework y MSBuild fallaba con `MSB3644`. Se resuelve con el DevPack (~100 MB),
mucho más ligero que añadir la carga completa de VS (varios GB):

```powershell
winget install --id Microsoft.DotNet.Framework.DeveloperPack_4 --source winget
```

Con eso, compilar no necesita abrir Visual Studio:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
    AstroKFocuser.csproj /p:Configuration=Release
```

El `.csproj` referencia los ensamblados ASCOM desde
`C:\Program Files (x86)\ASCOM\Platform 6 Developer Components\Components\Platform6\`. Ojo al
detalle: la DLL se llama `ASCOM.DeviceInterfaces.dll` (plural) pero el *namespace* es
`ASCOM.DeviceInterface` (singular).

## Extensiones del protocolo

El firmware acepta además estos comandos (ver `FocuserProtocol.h`):

| Comando | Qué hace |
|---|---|
| `:CG#` | Devuelve la configuración completa (el mismo JSON de `/api/setup/config`) en base64 |
| `:CS<b64>#` | La guarda tras validarla. Responde `OK#` o `ER#` |
| `:CR#` | Reset de fábrica: borra la configuración y reinicia. Responde `OK#` antes de irse |
| `:DM#` | Modo en que arrancó el equipo: `FOCUSER#` o `ROTATOR#` |
| `:DS#` | Banderas de `DRV_STATUS` del driver, en texto |
| `:WI#` | Estado de red: `STA:<ip>#`, `AP:<ip>#` u `OFF:0.0.0.0#` |
| `:WR#` | Reinicia el equipo, para aplicar los cambios de WiFi |
| `:DH#` | Para qué se compiló el firmware: `<motor>;<chip_id>#` (p. ej. `35BY48;5#`; 5 = C3, 9 = S3) |
| `:UB<tam>,<md5>#` | Empieza una actualización de firmware a la partición OTA libre. `OK#` o `ER:<motivo>#` |
| `:UB<tam>,<md5>,W#` | Lo mismo para la web: la imagen LittleFS va a su partición de datos (desde 1.4.2) |
| `:UD<offset hex>,<b64>#` | Un trozo de la imagen (1 KB). El offset tiene que ser el siguiente byte esperado |
| `:UF<offset hex>,<n hex>#` | `n` bytes a 0xFF sin mandarlos, hasta 64 KB (desde 1.4.2; solo se usa para la web) |
| `:US#` | Cuántos bytes lleva escritos, en hex. Sirve para reanudar si un trozo se queda sin respuesta |
| `:UE#` | Verifica el MD5, cambia la partición de arranque y reinicia |
| `:UA#` | Cancela la actualización; el equipo sigue con su firmware |

### Actualización de firmware

La pestaña *Actualizar firmware* graba por USB una imagen de aplicación y, si el `.bin` la
lleva, la de la web del equipo. El binario puede venir
del catálogo de `https://astrok.eu/ascom/fw.php` (ver `bin/fw.php`, que lista los `.bin` de
su carpeta) o de un fichero local, pero en los dos casos el nombre tiene que ser el que pone
`scripts/build_fw.ps1`: `astrok_<motor>_v<x.y.z>_<aaaammdd>.bin`. Del nombre salen el motor y
la versión; de la cabecera de la imagen, el chip.

- Otro chip: se bloquea (en el driver y otra vez en el firmware). Esa imagen se graba sin
  queja, pero el bootloader la rechaza y el equipo se queda en un bucle de reinicios.
- Otro motor, o una versión que no es más nueva: se pide confirmación.
- La descarga se comprueba contra el `size` y el `sha256` del catálogo.

Al conectar se consulta el catálogo en segundo plano, y si hay una versión más nueva para el
motor del equipo la pestaña se ilumina. La imagen va a la partición OTA libre: si se corta, el
equipo sigue arrancando con el firmware de antes. Un equipo con firmware anterior a `:DH#` no
sabe actualizarse así: hay que grabarlo una vez con `flash.ps1`.

Dos cosas medidas en la placa que explican el diseño: el USB nativo del C3 retiene respuestas
cortas hasta que se escribe algo detrás, por eso el firmware hace `Serial.flush()` tras cada
respuesta de actualización (sin él la transferencia se paraba a medias); y por si aun así se
pierde algo está `:US#`, con el que el driver reanuda sin duplicar ni saltar datos. Un MB tarda
unos 14 s.

Al conectar, la lista de internet solo enseña el firmware del motor del equipo. El de otro
motor se puede grabar a propósito abriéndolo como fichero local, y entonces se pide
confirmación. Mientras se graba, la pantalla del equipo muestra `UPDATING...` (o
`UPDATING WEB...`) con el porcentaje y una barra. Si el driver deja de mandar datos durante
30 s, el equipo cancela la actualización.

#### Aviso de app nueva (desde 1.4.3)

Al abrir la ventana se consulta el mismo `fw.php`, que además de los `.bin` lista los
instaladores `AstroK<Focuser|Rotator>-Setup-<x.y.z>.exe` de su carpeta (clave `apps`). Si hay
uno más nuevo que la app que corre, del mismo producto, sale un enlace bajo el pie de la
ventana que abre la descarga en el navegador (solo URLs `https://`). Sin red no se dice nada.
Para que funcione, los instaladores se suben junto a los firmwares.

#### La web en el mismo paquete (desde 1.4.2)

`scripts/build_fw.ps1` pone detrás del programa la imagen LittleFS de `data/` (la web de
configuración), recortada al último bloque escrito, y cierra el fichero con 12 bytes: tamaño
del programa y de la web (uint32 little endian) y la marca `AKPK`. Un `.bin` sin esa marca es
solo el programa, como los de antes, y se sigue aceptando. `fw.php` no cambia: calcula el
tamaño y el sha256 del fichero entero.

El driver graba primero el programa, espera al reinicio y, ya con el firmware nuevo arrancado
(es el que entiende `,W` y `:UF#`), manda la web y espera a otro reinicio. `mklittlefs`
empieza a colocar ficheros en un bloque al azar, así que la imagen es casi toda flash vacía:
los tramos a 0xFF van con `:UF#` y lo que viaja de verdad son unos 150 KB.

La web no tiene la red de seguridad del programa: solo hay una partición y se escribe encima.
Si se corta a medias, `LittleFS.begin(true)` la formatea al arrancar y la página de
configuración sale vacía hasta repetir la actualización. El programa y la configuración (en
NVS) no se tocan. Si el programa se ha grabado y la web no, la ventana lo dice así.

Compatibilidad: la app 1.4.1 y anteriores rechazan los paquetes por demasiado grandes, y el
firmware 1.4.1 y anteriores contestan `ER:PARAMS` a `,W`. Por eso el orden es siempre
programa primero.

El JSON viaja en **base64** a propósito: los SSID y contraseñas pueden contener `:` o `#`, que
son los delimitadores del protocolo, y romperían el marcado. Probado con
`{"ssid":"Red#Con:Simbolos","password":"p#ss:w0rd"}`, que sobrevive intacto.

## Notas de implementación

- **Posición máxima**: antes la posición viajaba en 4 cifras hex (65535 como mucho). Desde
  la 1.4 el firmware manda al menos 4 y las que hagan falta, así que el tope es
  `DeviceLink.MaxPasosEnfocador` (999999, igual que `MAX_FOCUSER_STEPS` del firmware). Al
  conectar se lee el `max_steps` real del equipo con `:CG#`; el valor del perfil ASCOM es solo
  el respaldo si el firmware es antiguo y no conoce esa extensión.
- **Temperatura**: el dispositivo no lleva sensor. `TempCompAvailable` es `false` y
  `Temperature` lanza `PropertyNotImplementedException`, que es lo que espera ASCOM.
- **Perfil ausente**: si el driver todavía no está registrado, `Profile.GetValue()` lanza
  `DriverNotRegisteredException`. `ReadProfile()` parte de valores por defecto y solo lee el
  perfil si `IsRegistered()` dice que existe, para no reventar en el constructor.
- **Fallo de conexión**: si el handshake `:GV#` falla, el puerto se cierra y se libera. Dejarlo
  abierto lo bloquearía para el siguiente intento.
- **Apertura del puerto**: abrir el puerto resetea el ESP32 (USB nativo). El firmware atiende el
  protocolo serie durante su propio arranque (`AlpacaServer::setTickCallback`), así que responde
  de inmediato; aun así el driver espera 300 ms y vacía el buffer por si queda algo del banner.
- **`Move` es absoluto**: `Absolute` devuelve `true`; el firmware posiciona en pasos absolutos.

## Alternativa sin driver

El firmware también expone **ASCOM ALPACA** por WiFi (con descubrimiento UDP), que N.I.N.A.
encuentra solo y no necesita ni driver ni cable. Este driver existe para tener una vía cableada
y fiable cuando el WiFi no es una opción — y en la variante C3, para que el USB funcione
siquiera (DTR).
