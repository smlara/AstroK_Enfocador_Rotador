AstroK Enfocador - driver ASCOM y herramienta de configuración
====================================================================

QUÉ SE HA INSTALADO
-------------------
1. El driver ASCOM "AstroK Focuser", para N.I.N.A. y cualquier otro
   programa de captura compatible con ASCOM.
2. "AstroK Enfocador" (menú Inicio y, si lo elegiste, escritorio): una
   aplicación que abre la misma ventana de configuración sin pasar por
   N.I.N.A. ni por el selector de ASCOM. Sirve para dejar el equipo
   configurado en un portátil limpio, o para cambiar la WiFi sin abrir el
   programa de captura. No toca el perfil ASCOM: todo va contra el equipo
   por USB.

USO EN N.I.N.A.
---------------
Equipment -> Focuser -> ASCOM -> "AstroK Focuser".
Pulsa el engranaje para elegir el puerto COM y la posición máxima, y conecta.

El equipo tiene que estar en modo enfocador (lo fija el puente de modo de la
placa al arrancar). Si está en modo rotador, el driver lo avisa al conectar.

VENTANA DE CONFIGURACIÓN
------------------------
Desde el driver se abre con el engranaje, con el driver desconectado. Tiene
tres pestañas:

- Enfocador: puerto COM (con búsqueda automática del equipo), conectar y
  desconectar, posición en vivo, saltos de ±10 / ±100 / ±500 pasos, parada,
  mover a una posición, fijar la posición actual a un valor (para recuperar
  la referencia si se perdió el cero) y la posición máxima, que se guarda en
  el propio equipo.
- Avanzados: corriente del motor, corriente en reposo, micropasos, invertir
  el sentido de giro y detección de colisión (StallGuard). Se aplican al
  guardar, sin reiniciar el equipo. Desde aquí también se restauran los
  valores de fábrica.
- WiFi: IP actual, nombre y contraseña del punto de acceso propio y lista de
  redes conocidas. Al guardar se reinicia el equipo, porque la red solo se
  configura al arrancar.

La ventana está en español e inglés; el idioma se elige abajo.

POSICIÓN MÁXIMA
---------------
El recorrido admite hasta 999999 pasos, y ese es también el valor de fábrica.
Ajusta la posición máxima al recorrido real de tu enfocador para no forzarlo
contra el final de carrera.

ALTERNATIVA SIN CABLE (ASCOM ALPACA)
------------------------------------
El equipo también se ofrece por WiFi como dispositivo ASCOM ALPACA, con
descubrimiento automático: N.I.N.A. lo encuentra solo, sin este driver y sin
cable USB. Este driver existe para tener una vía cableada y fiable cuando el
WiFi no es una opción.

REQUISITOS
----------
- ASCOM Platform 6 o posterior (https://ascom-standards.org). Sin ella, el
  driver no se puede usar desde los programas de captura.
- El equipo conectado por USB. Al abrir el puerto, el equipo se reinicia; es
  normal y tarda unos segundos en contestar.

DESINSTALAR
-----------
Desde "Agregar o quitar programas" de Windows. La desinstalación borra también
el registro del driver en ASCOM.
