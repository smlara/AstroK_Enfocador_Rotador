# AstroK — Controlador DIY de Enfocador y Rotador para Astrofotografía

[![License: GPL v3](https://img.shields.io/badge/Firmware-GPL%20v3-blue.svg)](LICENSE)
[![License: CERN OHL v2-S](https://img.shields.io/badge/Hardware-CERN%20OHL%20v2--S-orange.svg)](LICENSE-HARDWARE)
[![Platform: ESP32-C3](https://img.shields.io/badge/Plataforma-ESP32--C3-teal.svg)](https://www.espressif.com/)
[![Protocol: ASCOM](https://img.shields.io/badge/Protocolo-ASCOM%20%7C%20ALPACA-lightgrey.svg)]()

> **[English version](README.md)**

Controlador DIY totalmente open-source para un **enfocador** de telescopio o un **rotador** de cámara, basado en un **ESP32-C3** y un driver de motor paso a paso **TMC2209**. La misma placa y el mismo firmware sirven para los dos equipos, que se eligen con un jumper. Se integra con software de captura como N.I.N.A., Sequence Generator Pro y otros mediante su propio **driver ASCOM** por USB, o mediante **ASCOM ALPACA** por WiFi.

---

## Características

- **Enfocador o rotador** — una placa, un firmware; el modo lo decide un jumper
- **Driver ASCOM para Windows** (USB) y **ASCOM ALPACA** (WiFi, con descubrimiento)
- **Protocolo serie propio** por USB, el que usa el driver ASCOM
- **Control manual** mediante encoder rotatorio (x10 / x100 / x500 pasos, o x0,1° / x1° en modo rotador)
- **Pantalla OLED** (128×32) que muestra la posición y el paso activo
- **Página web de configuración** servida por el propio equipo (inglés / español)
- **Actualización de firmware por USB** desde la app de configuración, incluida la página web
- **Funcionamiento silencioso** (StealthChop) y detección opcional de bloqueo sin sensores
- **Firmware versión 1.4.3** · **App ASCOM versión 2.0.1**

---

## Hardware

| Componente | Descripción |
|------------|-------------|
| ESP32-C3 SuperMini | Microcontrolador con USB nativo y WiFi |
| Módulo TMC2209 | Driver de motor paso a paso, configurado por UART |
| Motor paso a paso | NEMA 17 (17HS4023), o motor con reductora 35BY48 (120:1) para enfocadores |
| OLED SSD1306 | Pantalla I2C 128×32 px |
| Encoder rotatorio | EC11 o compatible, con pulsador |
| Alimentación | 12 V para el motor |

El diseño de la PCB y la lista de materiales están en [pcb/](pcb/).

---

## Lista de materiales (BOM)

| Cant. | Componente | Especificaciones |
|:-----:|------------|-----------------|
| 1 | ESP32-C3 SuperMini | USB-C |
| 1 | Módulo TMC2209 | Modo UART |
| 1 | Motor paso a paso | 17HS4023 o 35BY48 (120:1) |
| 1 | Pantalla OLED | SSD1306, 128×32 px, I2C, 3.3 V |
| 1 | Encoder rotatorio | EC11 o compatible, con pulsador |
| 1 | Jumper | Selección de modo enfocador / rotador |
| 1 | Conector DC | DC-005, entrada de 12 V |
| 1 | Regulador de tensión | 7805, 12 V → 5 V para la lógica |
| — | PCB | Lista completa en [pcb/](pcb/) |

---

## Asignación de pines (ESP32-C3 SuperMini)

| GPIO | Señal           | Componente        |
|:----:|-----------------|-------------------|
| 5    | STEP            | TMC2209           |
| 6    | DIR             | TMC2209           |
| 7    | EN              | TMC2209           |
| 21   | UART TX         | TMC2209           |
| 20   | UART RX         | TMC2209           |
| 10   | DIAG            | TMC2209 (bloqueo) |
| 1    | CLK             | Encoder rotatorio |
| 3    | DT              | Encoder rotatorio |
| 0    | SW (botón)      | Encoder rotatorio |
| 4    | Selector de modo | Jumper (LOW = rotador) |
| 8    | SDA             | OLED (I2C)        |
| 9    | SCL             | OLED (I2C)        |

---

## Comunicación

| Interfaz | Detalles |
|----------|----------|
| USB serie | **115200 bps**, juego de comandos propio que usa el driver ASCOM |
| ASCOM ALPACA | HTTP en el puerto **11111**, descubrimiento UDP en el puerto **32227** |
| Configuración web | `http://<ip-del-equipo>/` |

Las posiciones viajan en hexadecimal con al menos 4 cifras, así que el enfocador llega hasta 999999 pasos.

---

## Compilación y carga

Este proyecto usa [PlatformIO](https://platformio.org/).

```bash
# Clonar el repositorio
git clone https://github.com/smlara/AstroK_Enfocador_Rotador.git
cd AstroK_Enfocador_Rotador

# Compilar y cargar (motor NEMA 17)
pio run -e astrok-c3 -t upload

# O para el motor con reductora 35BY48
pio run -e astrok-c3-35by48 -t upload

# Cargar la página web de configuración
pio run -e astrok-c3 -t uploadfs
```

Con el firmware ya cargado, las siguientes actualizaciones se pueden hacer desde la app ASCOM por USB.

### Driver ASCOM

El driver de Windows y la app de configuración están en [ascom-driver/](ascom-driver/). Los instaladores se generan con `ascom-driver/build.ps1` y necesitan la [ASCOM Platform 6](https://ascom-standards.org/).

### Dependencias (instaladas automáticamente por PlatformIO)

- `waspinator/AccelStepper ^1.64`
- `adafruit/Adafruit SSD1306 ^2.5.13`
- `adafruit/Adafruit GFX Library ^1.11.11`
- `teemuatlut/TMCStepper ^0.7.3`
- `bblanchon/ArduinoJson ^7.0.4`

---

## Controles del encoder

| Acción | Efecto |
|--------|--------|
| Girar a la derecha | Avanzar el paso activo |
| Girar a la izquierda | Retroceder el paso activo |
| Pulsación corta | Cambiar el paso (x10 / x100 / x500 pasos, o x0,1° / x1°) |
| Pulsación larga (≥ 3 s) | Poner la posición a 0 |
| Mantener pulsado al encender | Forzar el modo punto de acceso WiFi |

---

## Licencia

Este proyecto utiliza un modelo de **doble licencia**:

### Uso no comercial (open source)

- **Firmware y software** (todo el código fuente en `src/`, `data/`, `ascom-driver/` y `scripts/`): [GNU General Public License v3.0](LICENSE)
- **Hardware** (esquemas, diseños PCB y archivos mecánicos en `pcb/`): [CERN Open Hardware Licence v2 - Strongly Reciprocal](LICENSE-HARDWARE)

Eres libre de usar, estudiar, modificar y compartir este proyecto para **uso personal, educativo o no comercial**, siempre que cualquier trabajo derivado permanezca abierto bajo las mismas licencias.

### Uso comercial

**Cualquier uso comercial — incluyendo, entre otros, la fabricación, venta o integración de este diseño en un producto o servicio — está estrictamente prohibido sin el permiso previo y por escrito del autor.**

Si estás interesado en una licencia comercial, abre una [consulta comercial](../../issues/new?labels=commercial-inquiry&title=Consulta+Licencia+Comercial) en este repositorio.

---

## Autor

**Sergio Martínez** — Entusiasta de la astrofotografía  
Preguntas, contribuciones y consultas comerciales: abre un [issue](../../issues) o inicia una [discusión](../../discussions).
