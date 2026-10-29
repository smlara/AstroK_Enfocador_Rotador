<#
.SINOPSIS
    Compila el firmware de cada motor y deja los binarios en bin\ con el motor,
    la version y la fecha: astrok_17HS4023_v1.4.2_20261029.bin y
    astrok_35BY48_v1.4.2_20261029.bin.

.DESCRIPCION
    El modo (rotador o enfocador) lo decide el jumper al arrancar, no la
    compilacion, asi que cada binario vale para los dos modos. Lo que cambia de
    un entorno de platformio.ini a otro es el motor y sus valores de fabrica:
    se compila cada [env:...] y el motor sale de su custom_motor; el que no lo
    tiene es el de serie, el 17HS4023.

    La version es custom_version de platformio.ini, tal cual. El .bin es un
    paquete para la actualizacion por USB de la app ASCOM: la aplicacion (la
    que va en 0x10000), detras la imagen LittleFS de la web (data\) recortada
    a lo que ocupa, y al final 12 bytes: tamano de la aplicacion y de la web
    (uint32 little endian) y la marca "AKPK". No lleva bootloader ni tabla de
    particiones: para una placa nueva sigue haciendo falta flash.ps1.

    La app 1.4.1 y anteriores no saben separar la web y rechazan el paquete
    por grande; el firmware 1.4.1 y anteriores no aceptan la web. La app nueva
    graba primero la aplicacion y, ya con ella arrancada, la web.

.EJEMPLOS
    .\scripts\build_fw.ps1                   # todos los motores
    .\scripts\build_fw.ps1 -Motor 35BY48     # solo uno
#>

param(
    [string]$Motor = ""
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
Set-Location $raiz

$motorDeSerie = "17HS4023"

function Abortar([string]$texto) {
    Write-Host "ERROR: $texto" -ForegroundColor Red
    exit 1
}

$ini = Join-Path $raiz "platformio.ini"

# --- Version ------------------------------------------------------------
$linea = Select-String -Path $ini -Pattern '^\s*custom_version\s*=\s*(\S+)' | Select-Object -First 1
if (-not $linea) { Abortar "no encuentro custom_version en platformio.ini." }
$version = $linea.Matches[0].Groups[1].Value
if ($version -notmatch '^\d+\.\d+\.\d+$') { Abortar "custom_version debe tener tres cifras (x.y.z), y es '$version'." }
$fecha = Get-Date -Format "yyyyMMdd"

# --- Entornos y su motor ------------------------------------------------
# Las secciones comentadas (; [env:...]) no cuentan, igual que para pio.
$motores = [ordered]@{}
$actual = $null
foreach ($l in Get-Content $ini) {
    if ($l -match '^\[env:([^\]]+)\]') { $actual = $Matches[1]; $motores[$actual] = $motorDeSerie }
    elseif ($l -match '^\[') { $actual = $null }
    elseif ($actual -and $l -match '^\s*custom_motor\s*=\s*(\S+)') { $motores[$actual] = $Matches[1] }
}
if ($motores.Count -eq 0) { Abortar "no hay ningun [env:...] activo en platformio.ini." }
if ($Motor -ne "") {
    $elegidos = [ordered]@{}
    foreach ($e in $motores.Keys) { if ($motores[$e] -eq $Motor) { $elegidos[$e] = $Motor } }
    if ($elegidos.Count -eq 0) { Abortar "ningun entorno lleva el motor '$Motor'. Hay: $(($motores.Values | Select-Object -Unique) -join ', ')." }
    $motores = $elegidos
}

$destino = Join-Path $raiz "bin"
New-Item -ItemType Directory -Force $destino | Out-Null

# --- Compilacion --------------------------------------------------------
$hechos = @()
foreach ($entorno in $motores.Keys) {
    $m = $motores[$entorno]
    Write-Host ""
    Write-Host "==> Compilando $m ($entorno)" -ForegroundColor Cyan
    & pio run -e $entorno
    if ($LASTEXITCODE -ne 0) { Abortar "fallo la compilacion de $entorno." }

    # El nombre interno lo pone rename_firmware.py; se coge el .bin de la
    # aplicacion recien generado en vez de reconstruir ese nombre aqui.
    $bin = Get-ChildItem (Join-Path $raiz ".pio\build\$entorno") -Filter *.bin |
        Where-Object { $_.Name -notin @("bootloader.bin", "partitions.bin", "littlefs.bin") } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $bin) { Abortar "no encuentro el .bin de $entorno." }

    $app = [IO.File]::ReadAllBytes($bin.FullName)

    # La web es la misma para todos los motores, pero buildfs la ajusta a la
    # particion del entorno, asi que se genera en cada uno.
    & pio run -e $entorno -t buildfs
    if ($LASTEXITCODE -ne 0) { Abortar "fallo la imagen de la web de $entorno." }
    $fs = [IO.File]::ReadAllBytes((Join-Path $raiz ".pio\build\$entorno\littlefs.bin"))

    # Lo que queda detras del ultimo byte escrito es flash borrada (0xFF); no
    # hace falta mandarla. Se corta en bloque entero de LittleFS (4 KB).
    $usado = $fs.Length
    while ($usado -gt 0 -and $fs[$usado - 1] -eq 0xFF) { $usado-- }
    $usado = [int]([math]::Ceiling($usado / 4096) * 4096)
    if ($usado -eq 0) { Abortar "la imagen de la web de $entorno esta vacia." }

    $cola = New-Object byte[] 12
    [BitConverter]::GetBytes([uint32]$app.Length).CopyTo($cola, 0)
    [BitConverter]::GetBytes([uint32]$usado).CopyTo($cola, 4)
    [Text.Encoding]::ASCII.GetBytes("AKPK").CopyTo($cola, 8)

    $salida = Join-Path $destino ("astrok_{0}_v{1}_{2}.bin" -f $m, $version, $fecha)
    $f = [IO.File]::Create($salida)
    try {
        $f.Write($app, 0, $app.Length)
        $f.Write($fs, 0, $usado)
        $f.Write($cola, 0, $cola.Length)
    } finally { $f.Close() }
    Write-Host ("    aplicacion {0:N0} bytes + web {1:N0} bytes" -f $app.Length, $usado)
    $hechos += $salida
}

Write-Host ""
foreach ($h in $hechos) { Write-Host "Listo: $h" -ForegroundColor Green }
