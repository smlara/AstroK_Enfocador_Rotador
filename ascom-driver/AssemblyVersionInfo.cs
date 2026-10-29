using System.Reflection;

// Version unica de todo el producto Windows: la comparten los dos DLL del
// driver y los dos ejecutables de la herramienta, que la incluyen desde sus
// .csproj. Antes solo la llevaban los DLL, asi que los .exe se publicaban como
// 0.0.0.0 y en las propiedades del archivo no habia forma de saber que version
// habia instalada.
//
// Debe cuadrar con:
//   - AppVersion de installer-focuser.iss e installer-rotator.iss
// DriverVersion de Driver.cs y Rotator.cs va aparte: ASCOM lo define como
// "m.n", dos componentes, asi que va en 2.0 y no sigue al parche.
[assembly: AssemblyVersion("2.0.1.0")]
[assembly: AssemblyFileVersion("2.0.1.0")]
[assembly: AssemblyProduct("AstroK")]
