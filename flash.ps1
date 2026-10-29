<#
.SINOPSIS
    Deja el equipo como recien salido de fabrica: firmware, sistema de archivos
    web y NVS en blanco.

.DESCRIPCION
    Flashear NO borra la NVS. Los valores por defecto de Config.h solo se
    escriben cuando la clave "config" no existe (ver main.cpp), asi que una
    placa que ya tiene configuracion guardada se queda con la vieja para
    siempre: cambiar DEFAULT_ROTATOR_MICROSTEPS a 8 no sirve de nada si en la
    NVS siguen los 32 de una version anterior, y encima el firmware los aplica
    de verdad al motor. Lo mismo pasa con la basura que dejan las pruebas de
    placa. Por eso este script borra la particion NVS explicitamente con
    esptool despues de flashear, y no da por hecho que el flasheo lo haga.

    Se borra solo la region de la NVS, sacando offset y tamano de la tabla de
    particiones que hay grabada en la placa: hardcodear 0x9000 vale hoy y
    miente el dia que se cambie de tabla.

    Sin -Entorno, pregunta cual flashear de entre los [env:...] de
    platformio.ini.

.EJEMPLOS
    .\flash.ps1                      # pregunta el entorno, compila, flashea, sube la web y limpia la NVS
    .\flash.ps1 -SoloNvs             # solo limpia la NVS, sin tocar el firmware
    .\flash.ps1 -BorrarTodo          # borra el chip entero antes de flashear
    .\flash.ps1 -Puerto COM7 -Entorno astrok-c3-35by48
#>

param(
    [string]$Entorno = "",
    [string]$Puerto = "",
    [switch]$SoloNvs,
    [switch]$BorrarTodo,
    [switch]$SinVerificar
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $raiz

function Paso([string]$texto) {
    Write-Host ""
    Write-Host "==> $texto" -ForegroundColor Cyan
}

function Abortar([string]$texto) {
    Write-Host "ERROR: $texto" -ForegroundColor Red
    exit 1
}

# Escribe 0xFF en una region, que es como queda la flash borrada. Se hace asi
# y no con erase_region/erase_flash porque esos dos solo existen en el stub de
# esptool, y en el C3 el stub deja de responder por el USB nativo (de ahi el
# upload_flags = --no-stub de platformio.ini); la ROM sabe escribir pero no
# borrar por regiones. Comprimido, el bloque de 0xFF ocupa casi nada.
function BorrarRegion([int]$offset, [int]$tam) {
    $vacio = Join-Path $env:TEMP "astrok_vacio_$tam.bin"
    if (-not (Test-Path $vacio) -or (Get-Item $vacio).Length -ne $tam) {
        & $python -c "open(r'$vacio','wb').write(b'\xff'*$tam)"
    }
    # Out-Host para que la salida de esptool no se cuele en el valor devuelto.
    & $python $paqueteEsptool --port $Puerto --no-stub write_flash -z ("0x{0:X}" -f $offset) $vacio | Out-Host
    return ($LASTEXITCODE -eq 0)
}

# --- Entorno ------------------------------------------------------------
# Sin -Entorno se pregunta: hay variantes (p. ej. la del motor 35BY48) que solo
# cambian los valores de fabrica, y flashear la que no es pasa desapercibido.
$entornos = @(Select-String -Path (Join-Path $raiz "platformio.ini") -Pattern '^\[env:([^\]]+)\]' |
    ForEach-Object { $_.Matches[0].Groups[1].Value })
if ($entornos.Count -eq 0) { Abortar "no hay ningun [env:...] activo en platformio.ini." }
if ($Entorno -eq "") {
    if ($entornos.Count -eq 1) {
        $Entorno = $entornos[0]
    } else {
        Write-Host "Entornos disponibles:"
        for ($k = 0; $k -lt $entornos.Count; $k++) { Write-Host ("  {0}) {1}" -f ($k + 1), $entornos[$k]) }
        $eleccion = Read-Host "Cual flasheo? (1-$($entornos.Count))"
        $n = 0
        if (-not [int]::TryParse($eleccion, [ref]$n) -or $n -lt 1 -or $n -gt $entornos.Count) {
            Abortar "eleccion no valida: '$eleccion'."
        }
        $Entorno = $entornos[$n - 1]
    }
} elseif ($entornos -notcontains $Entorno) {
    Abortar "el entorno '$Entorno' no existe en platformio.ini. Hay: $($entornos -join ', ')."
}

# --- Puerto -------------------------------------------------------------
if ($Puerto -eq "") {
    $salida = & pio device list --json-output 2>$null
    if ($LASTEXITCODE -ne 0) { Abortar "no se ha podido ejecutar 'pio'. Abre la consola de PlatformIO." }
    $puertos = $salida | ConvertFrom-Json
    # 303A es Espressif: si hay varios cacharros enchufados, el ESP32 es ese.
    $candidatos = @($puertos | Where-Object { $_.hwid -match "VID:PID=303A" })
    if ($candidatos.Count -eq 0) { $candidatos = @($puertos) }
    if ($candidatos.Count -eq 0) { Abortar "no hay ningun puerto serie. Conecta la placa." }
    if ($candidatos.Count -gt 1) {
        Write-Host "Hay varios puertos; usa -Puerto para elegir:" -ForegroundColor Yellow
        $candidatos | ForEach-Object { Write-Host ("  " + $_.port + "  " + $_.description) }
        Abortar "puerto ambiguo."
    }
    $Puerto = $candidatos[0].port
}
Write-Host "Puerto:  $Puerto"
Write-Host "Entorno: $Entorno"

# --- esptool ------------------------------------------------------------
# El que trae PlatformIO, para no depender de un pip install aparte.
$paqueteEsptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
if (-not (Test-Path $paqueteEsptool)) { Abortar "no encuentro esptool.py en $paqueteEsptool" }
$python = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
if (-not (Test-Path $python)) { $python = "python" }

if ($BorrarTodo) {
    # El tamano real se le pregunta al chip, no se supone.
    $id = & $python $paqueteEsptool --port $Puerto --no-stub flash_id 2>&1 | Out-String
    if ($id -notmatch 'Detected flash size: (\d+)MB') { Abortar "no he podido leer el tamano de la flash." }
    $tamFlash = [int]$Matches[1] * 1024 * 1024
    Paso "Borrando el chip entero ($($Matches[1]) MB)"
    if (-not (BorrarRegion 0 $tamFlash)) { Abortar "fallo el borrado completo." }
}

# --- Firmware y sistema de archivos ------------------------------------
if (-not $SoloNvs) {
    Paso "Compilando y flasheando el firmware"
    & pio run -e $Entorno -t upload --upload-port $Puerto
    if ($LASTEXITCODE -ne 0) { Abortar "fallo el flasheo del firmware." }

    Paso "Subiendo el sistema de archivos (interfaz web)"
    & pio run -e $Entorno -t uploadfs --upload-port $Puerto
    if ($LASTEXITCODE -ne 0) { Abortar "fallo la subida de LittleFS." }
}

# --- NVS ----------------------------------------------------------------
# Va al final a proposito: entre paso y paso la placa arranca y el firmware
# reescribe la configuracion, asi que el ultimo arranque tiene que ser el que
# encuentre la NVS vacia y la recree con los valores de Config.h.
# La tabla se lee de la propia placa (0x8000 en ESP32): es la que manda, y el
# partitions.bin de .pio\build no sirve porque uploadfs lo borra.
$tabla = Join-Path $env:TEMP "astrok_particiones.bin"
& $python $paqueteEsptool --port $Puerto --no-stub read_flash 0x8000 0xC00 $tabla | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tabla)) { Abortar "no he podido leer la tabla de particiones de la placa." }

$bytes = [System.IO.File]::ReadAllBytes($tabla)
$offset = 0
$tam = 0
for ($i = 0; $i -lt ($bytes.Length - 32); $i += 32) {
    if ($bytes[$i] -ne 0xAA -or $bytes[$i + 1] -ne 0x50) { continue }
    $etiqueta = [System.Text.Encoding]::ASCII.GetString($bytes, $i + 12, 16).Trim([char]0)
    if ($etiqueta -ne "nvs") { continue }
    $offset = [BitConverter]::ToUInt32($bytes, $i + 4)
    $tam = [BitConverter]::ToUInt32($bytes, $i + 8)
    break
}
if ($tam -eq 0) { Abortar "no he encontrado la particion 'nvs' en la tabla de particiones." }

$offsetHex = "0x{0:X}" -f $offset
$tamHex = "0x{0:X}" -f $tam
Paso "Borrando la NVS ($offsetHex, $tamHex bytes)"
if (-not (BorrarRegion $offset $tam)) { Abortar "fallo el borrado de la NVS." }

# --- Verificacion -------------------------------------------------------
# Leer lo que ha quedado dentro es justo el paso que faltaba cuando una config
# vieja sobrevivio a varios flasheos sin que se notara.
if (-not $SinVerificar) {
    $verificador = Join-Path $raiz "verificar_equipo.py"
    if (Test-Path $verificador) {
        Paso "Comprobando que el equipo arranca con los valores de fabrica"
        & $python $verificador $Puerto
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Aviso: no se ha podido verificar por USB. Revisalo a mano con el driver." -ForegroundColor Yellow
        }
    }
}

Write-Host ""
Write-Host "Listo. La placa arranca con los valores por defecto de Config.h." -ForegroundColor Green
Write-Host "Recuerda volver a meter las redes WiFi: de serie levanta su propio AP." -ForegroundColor Green
