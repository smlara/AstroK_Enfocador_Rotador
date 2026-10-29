<#
    Compila los dos productos y genera sus instaladores en ..\bin\, junto a
    los firmwares: es la carpeta que se sube al servidor con fw.php.

    Enfocador y rotador son productos distintos (mecánica y forma propias)
    aunque compartan electrónica, firmware y este código fuente. De ahí dos
    DLL, dos ejecutables y dos instaladores, generados de una sola pasada
    desde el mismo árbol para que no puedan divergir.

    No necesita abrir Visual Studio: usa MSBuild directamente. Requiere el
    .NET Framework Developer Pack (ver README) e Inno Setup.
#>

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# --- MSBuild: se localiza con vswhere en vez de cablear la ruta ---
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "No encuentro vswhere.exe: ¿está instalado Visual Studio?" }

$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw "No encuentro MSBuild." }

# --- Inno Setup: se instala por usuario, de ahí LOCALAPPDATA ---
$iscc = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $iscc) {
    throw "No encuentro ISCC.exe. Instala Inno Setup: winget install --id JRSoftware.InnoSetup"
}

# Cada producto: proyecto de la herramienta (que arrastra su DLL por
# referencia) y su propio script de instalador.
$productos = @(
    @{ Nombre = 'Enfocador'; Proyecto = 'AstroKFocuserTool.csproj'; Instalador = 'installer-focuser.iss' },
    @{ Nombre = 'Rotador';   Proyecto = 'AstroKRotatorTool.csproj'; Instalador = 'installer-rotator.iss' }
)

foreach ($p in $productos) {
    Write-Host ""
    Write-Host "=== $($p.Nombre) ===" -ForegroundColor Cyan

    Write-Host "Compilando..." -ForegroundColor Cyan
    & $msbuild $p.Proyecto /p:Configuration=Release /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion de $($p.Nombre)." }

    Write-Host "Generando el instalador..." -ForegroundColor Cyan
    & $iscc $p.Instalador | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { throw "Fallo el instalador de $($p.Nombre)." }
}

Write-Host ""
Write-Host "Listo:" -ForegroundColor Green
Get-ChildItem (Join-Path $PSScriptRoot '..\bin') -Filter 'AstroK*-Setup-*.exe' |
    ForEach-Object { Write-Host ("  {0}  ({1:N0} KB)" -f $_.Name, ($_.Length / 1KB)) }
