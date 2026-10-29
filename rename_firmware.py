Import("env")

# Obtener el nombre del entorno actual (ej. 'enfocador', 'rotador')
pioenv = env.get("PIOENV")

# Obtener la versión definida en platformio.ini (por defecto '1.0.0')
version = env.GetProjectOption("custom_version", "1.0.0")

# Determinar el nombre base del archivo
if "rotador" in pioenv.lower():
    base_name = "AstroKRotator"
elif "enfocador" in pioenv.lower():
    base_name = "AstroKFocuser"
else:
    base_name = "AstroKFirmware"

# Combinar nombre y versión
new_name = f"{base_name}-v{version}"

# Variante de motor (custom_motor en platformio.ini), para no confundir su
# binario con el del motor de serie
motor = env.GetProjectOption("custom_motor", "")
if motor:
    new_name += f"-{motor}"

# El firmware tiene que saber para que motor se compilo: el driver lo pregunta
# con :DH# para elegir el .bin de actualizacion que le corresponde. El de serie
# es el 17HS4023, el mismo nombre que usa scripts/build_fw.ps1 para los
# entornos sin custom_motor.
env.Append(CPPDEFINES=[("FW_MOTOR", env.StringifyMacro(motor or "17HS4023"))])

# Reemplazar la variable interna de PlatformIO para el nombre del binario
env.Replace(PROGNAME=new_name)
