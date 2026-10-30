; Instalador del driver ASCOM AstroK Focuser.
;
; No lo invoques a mano: usa build.ps1, que compila el driver y luego esto.
; La salida va a ..\bin\ (el de la raiz, junto a los firmwares y fw.php)

#define AppName        "AstroK Focuser (driver ASCOM)"
#define AppVersion     "2.1.0"
#define AppPublisher   "AstroK"
#define DriverDll      "ASCOM.AstroK.Focuser.dll"
#define DriverId       "ASCOM.AstroK.Focuser"
#define ToolExe        "AstroKFocuser.exe"

[Setup]
AppId={{9E2B4F13-6C8A-4A57-B2D1-5E7C9A3F1B44}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={commonpf32}\ASCOM\AstroKFocuser
DisableDirPage=yes
; Hasta la 1.4.0 el producto se llamaba AstroKeeper y vivia en otra carpeta.
; Con el mismo AppId Inno la reutilizaria; asi se pasa a la nueva y la
; antigua la limpia CurStepChanged.
UsePreviousAppDir=no
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName}
OutputDir=..\bin
OutputBaseFilename=AstroKFocuser-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=AstroKFocuser.ico
UninstallDisplayIcon={app}\{#ToolExe}
; El registro COM toca HKLM: hace falta elevación sí o sí.
PrivilegesRequired=admin
; El driver es AnyCPU y se registra para 32 y 64 bits; el instalador corre en
; modo nativo para tener acceso a las dos vistas del registro.
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
; El primero es el idioma por defecto; Inno ofrece elegir al arrancar y
; preselecciona el de Windows si esta en la lista.
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
; Textos propios del instalador, uno por idioma. %n es salto de linea.
es.ToolName=AstroK Enfocador
en.ToolName=AstroK Focuser
es.DesktopIcon=Crear un acceso directo en el escritorio
en.DesktopIcon=Create a desktop shortcut
es.Shortcuts=Accesos directos:
en.Shortcuts=Shortcuts:
es.Registering32=Registrando el driver (32 bits)...
en.Registering32=Registering the driver (32-bit)...
es.Registering64=Registrando el driver (64 bits)...
en.Registering64=Registering the driver (64-bit)...
es.NoAscom=No detecto ASCOM Platform instalado.%nEl driver lo necesita para funcionar.%n%n¿Continuar de todos modos?
en.NoAscom=ASCOM Platform does not seem to be installed.%nThe driver needs it to work.%n%nContinue anyway?
es.Done=Driver instalado.%n%nEn N.I.N.A.: Equipment -> Focuser -> ASCOM -> "AstroK Focuser".%nPulsa el engranaje para elegir el puerto COM antes de conectar.%n%nSe ha instalado también "AstroK Enfocador",%nque permite configurar el equipo sin abrir N.I.N.A.
en.Done=Driver installed.%n%nIn N.I.N.A.: Equipment -> Focuser -> ASCOM -> "AstroK Focuser".%nClick the gear to choose the COM port before connecting.%n%n"AstroK Focuser" was also installed,%nto set up the device without opening N.I.N.A.

[InstallDelete]
; Hasta la 1.2.1 se instalaba README.md, y alguna 1.4.0 de prueba un unico
; LEEME.txt o LEEME_es.txt; ahora van README_es.txt y README_en.txt.
Type: files; Name: "{app}\README.md"
Type: files; Name: "{app}\LEEME.txt"
Type: files; Name: "{app}\LEEME_es.txt"
; Accesos directos de cuando el producto se llamaba AstroKeeper.
Type: files; Name: "{autoprograms}\AstroKeeper Enfocador.lnk"
Type: files; Name: "{autodesktop}\AstroKeeper Enfocador.lnk"
Type: files; Name: "{autoprograms}\AstroKeeper Focuser.lnk"
Type: files; Name: "{autodesktop}\AstroKeeper Focuser.lnk"

[Files]
Source: "bin\Release\{#DriverDll}"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\{#ToolExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "AstroKFocuser.ico"; DestDir: "{app}"; Flags: ignoreversion
; Se instalan los dos README; al terminar se ofrece abrir el del idioma
; elegido (isreadme solo puede ir en uno, de ahi las entradas por idioma).
Source: "README-Enfocador_es.txt"; DestDir: "{app}"; DestName: "README_es.txt"; Flags: ignoreversion isreadme; Languages: es
Source: "README-Enfocador_es.txt"; DestDir: "{app}"; DestName: "README_es.txt"; Flags: ignoreversion; Languages: en
Source: "README-Enfocador_en.txt"; DestDir: "{app}"; DestName: "README_en.txt"; Flags: ignoreversion isreadme; Languages: en
Source: "README-Enfocador_en.txt"; DestDir: "{app}"; DestName: "README_en.txt"; Flags: ignoreversion; Languages: es

[Icons]
Name: "{autoprograms}\{cm:ToolName}"; Filename: "{app}\{#ToolExe}"; IconFilename: "{app}\AstroKFocuser.ico"
Name: "{autodesktop}\{cm:ToolName}"; Filename: "{app}\{#ToolExe}"; IconFilename: "{app}\AstroKFocuser.ico"; Tasks: iconoescritorio

[Tasks]
Name: "iconoescritorio"; Description: "{cm:DesktopIcon}"; GroupDescription: "{cm:Shortcuts}"

[Run]
; RegAsm /codebase graba la ruta absoluta de la DLL, por eso se registra ya
; instalada en {app} y no desde la carpeta de compilacion.
; Se registra con las DOS versiones: el Chooser de ASCOM es de 32 bits y
; N.I.N.A. de 64, y cada uno mira su propia vista del registro.
Filename: "{dotnet40}\RegAsm.exe"; Parameters: """{app}\{#DriverDll}"" /codebase /silent"; \
    StatusMsg: "{cm:Registering32}"; Flags: runhidden
Filename: "{dotnet4064}\RegAsm.exe"; Parameters: """{app}\{#DriverDll}"" /codebase /silent"; \
    StatusMsg: "{cm:Registering64}"; Flags: runhidden; Check: IsWin64

[UninstallRun]
Filename: "{dotnet40}\RegAsm.exe"; Parameters: """{app}\{#DriverDll}"" /unregister /silent"; \
    Flags: runhidden; RunOnceId: "UnregAsm32"
Filename: "{dotnet4064}\RegAsm.exe"; Parameters: """{app}\{#DriverDll}"" /unregister /silent"; \
    Flags: runhidden; Check: IsWin64; RunOnceId: "UnregAsm64"

[Code]
function InitializeSetup(): Boolean;
var
  Dummy: String;
begin
  Result := True;

  // Sin ASCOM Platform el driver no sirve de nada: sus ensamblados y el
  // perfil donde se registra vienen de ahi.
  if not RegQueryStringValue(HKLM32, 'SOFTWARE\ASCOM', 'PlatformVersion', Dummy) then
  begin
    if MsgBox(CustomMessage('NoAscom'), mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

// Hasta la 1.4.0 el driver se llamaba ASCOM.AstroKeeper.* y se instalaba
// en otra carpeta. Si sigue registrado, el selector de ASCOM enseñaría los
// dos drivers. Se desregistra con su propia DLL (RegAsm /unregister llama a
// su UnregisterASCOM, que lo saca del perfil ASCOM) y se borra la carpeta.
// Va en ssInstall, ANTES de que [Run] registre el nuevo: los dos comparten el
// GUID de COM, y desregistrar el viejo después borraría el registro del nuevo.
procedure QuitarInstalacionAntigua();
var
  Carpeta, Dll: String;
  Codigo: Integer;
begin
  Carpeta := ExpandConstant('{commonpf32}\ASCOM\AstroKeeperFocuser');
  Dll := Carpeta + '\ASCOM.AstroKeeper.Focuser.dll';
  if FileExists(Dll) then
  begin
    Exec(ExpandConstant('{dotnet40}\RegAsm.exe'), '"' + Dll + '" /unregister /silent', '',
         SW_HIDE, ewWaitUntilTerminated, Codigo);
    if IsWin64 then
      Exec(ExpandConstant('{dotnet4064}\RegAsm.exe'), '"' + Dll + '" /unregister /silent', '',
           SW_HIDE, ewWaitUntilTerminated, Codigo);
  end;
  if DirExists(Carpeta) then
    DelTree(Carpeta, True, True, True);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    QuitarInstalacionAntigua();
  if CurStep = ssPostInstall then
    MsgBox(CustomMessage('Done'), mbInformation, MB_OK);
end;
