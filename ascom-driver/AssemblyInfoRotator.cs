using System.Reflection;
using System.Runtime.InteropServices;

// Identidad del producto rotador. Ver la nota de AssemblyInfoFocuser.cs.
[assembly: AssemblyTitle("AstroK Rotator")]
[assembly: AssemblyDescription("ASCOM IRotatorV3 driver and setup tool for AstroK, over USB")]

// El registro COM lo hacen los atributos de la clase Rotator; a nivel de
// ensamblado no se expone nada mas.
[assembly: ComVisible(false)]
