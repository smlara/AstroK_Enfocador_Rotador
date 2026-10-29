using System.Reflection;
using System.Runtime.InteropServices;

// Identidad del producto enfocador: la comparten su DLL y su ejecutable, que
// lo compilan desde sus .csproj. Enfocador y rotador son productos distintos y
// cada uno tiene el suyo; antes habia un unico AssemblyInfo.cs compilado por
// los dos, asi que el DLL del rotador se presentaba como "Focuser".
//
// La version va aparte, en AssemblyVersionInfo.cs, que si es comun a los cuatro.
[assembly: AssemblyTitle("AstroK Focuser")]
[assembly: AssemblyDescription("ASCOM IFocuserV3 driver and setup tool for AstroK, over the Moonlite protocol")]

// El registro COM lo hacen los atributos de la clase Focuser; a nivel de
// ensamblado no se expone nada mas.
[assembly: ComVisible(false)]
