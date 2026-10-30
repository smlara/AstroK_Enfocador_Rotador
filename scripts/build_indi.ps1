<#
    Genera el paquete .deb de los drivers INDI en ..\bin\, junto a firmwares
    e instaladores de Windows: es la carpeta que se sube al servidor con fw.php.

    El paquete se compila en una máquina Linux por SSH, normalmente la propia
    Raspberry con PINS: así sale para su arquitectura (arm64) y con las
    dependencias de su distribución, que dpkg-shlibdeps saca de las
    bibliotecas contra las que se enlaza. Compilarlo en otra distribución daría
    un paquete que pide versiones que la Raspberry no tiene.

    Se manda el árbol de trabajo de indi-driver\, no el último commit.

    Uso:  .\build_indi.ps1 -Equipo usuario@host [-Clave C:\ruta\id_ed25519]
    Requiere en el equipo: cmake g++ pkg-config dpkg-dev libindi-dev (>= 2.0)
    libglib2.0-dev.
#>
param(
    [Parameter(Mandatory = $true)][string]$Equipo,
    [string]$Clave = (Join-Path $env:USERPROFILE '.ssh\id_ed25519')
)

$ErrorActionPreference = 'Stop'
$raiz = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $raiz 'bin'
$ssh = @('-i', $Clave, '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10')

# Se empaqueta en un .tar y se copia con scp: una tubería de PowerShell 5.1
# hacia ssh corrompe datos binarios.
$tar = Join-Path $env:TEMP 'astrok-indi-src.tar'
Remove-Item $tar -ErrorAction SilentlyContinue
& tar.exe -c -f $tar -C $raiz --exclude 'indi-driver/build' indi-driver
if ($LASTEXITCODE -ne 0) { throw "No se pudo empaquetar indi-driver." }

# El script remoto va como fichero y no como argumento de ssh: PowerShell
# 5.1 quita las comillas dobles de los argumentos de un programa externo.
# Con saltos LF, que bash no entiende los CRLF de Windows.
$remoto = @'
set -e
for p in cmake g++ pkg-config dpkg-shlibdeps; do
    command -v $p >/dev/null || { echo "Falta $p (apt install cmake g++ pkg-config dpkg-dev)"; exit 2; }
done
pkg-config --atleast-version=2.0 libindi || { echo "Falta libindi-dev 2.0 o posterior"; exit 2; }
pkg-config --exists gio-2.0 gio-unix-2.0 || { echo "Falta libglib2.0-dev"; exit 2; }
chmod -R u+rwX ~/astrok-indi-build 2>/dev/null || true
rm -rf ~/astrok-indi-build
mkdir -p ~/astrok-indi-build
# El tar.exe de Windows guarda los directorios sin permiso de escritura y
# con atributos propios (SCHILY.fflags) que GNU tar no conoce.
tar -x --warning=no-unknown-keyword -f /tmp/astrok-indi-src.tar -C ~/astrok-indi-build
chmod -R u+rwX ~/astrok-indi-build
rm -f /tmp/astrok-indi-src.tar /tmp/astrok-indi-build.sh
cd ~/astrok-indi-build/indi-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j4 > build.log 2>&1 || { grep -E "error" build.log; exit 1; }
grep -E "warning" build.log || true
cd build
cpack -G DEB >/dev/null
ls indi-astrok_*.deb
'@
$guion = Join-Path $env:TEMP 'astrok-indi-build.sh'
[IO.File]::WriteAllText($guion, $remoto.Replace("`r`n", "`n"))

Write-Host "Copiando las fuentes a $Equipo..." -ForegroundColor Cyan
& scp @ssh -q $tar $guion "${Equipo}:/tmp/"
if ($LASTEXITCODE -ne 0) { throw "No se pudo copiar a $Equipo." }
Remove-Item $tar, $guion

Write-Host "Compilando y empaquetando..." -ForegroundColor Cyan
$salida = & ssh @ssh $Equipo 'bash /tmp/astrok-indi-build.sh'
if ($LASTEXITCODE -ne 0) { $salida | ForEach-Object { Write-Host $_ }; throw "Fallo en $Equipo." }
$deb = ($salida | Where-Object { $_ -match '^indi-astrok_.*\.deb$' } | Select-Object -Last 1)
if (-not $deb) { $salida | ForEach-Object { Write-Host $_ }; throw "No se generó el .deb." }

# Al sacar versión nueva se borran las anteriores de esa arquitectura.
$arquitectura = ($deb -replace '^indi-astrok_[^_]+_(.+)\.deb$', '$1')
Get-ChildItem $bin -Filter "indi-astrok_*_$arquitectura.deb" | Remove-Item

& scp @ssh -q "${Equipo}:~/astrok-indi-build/indi-driver/build/$deb" (Join-Path $bin $deb)
if ($LASTEXITCODE -ne 0) { throw "No se pudo traer $deb." }

Write-Host ""
Write-Host "Listo:" -ForegroundColor Green
$f = Get-Item (Join-Path $bin $deb)
Write-Host ("  {0}  ({1:N0} KB)" -f $f.Name, ($f.Length / 1KB))
