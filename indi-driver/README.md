# AstroK INDI drivers

INDI drivers for the AstroK focuser and rotator, for Linux clients such as
KStars/Ekos or PINS. They talk to the device over **USB** or **Bluetooth LE**
with the same serial protocol as the Windows ASCOM driver, so WiFi is not
needed.

- `indi_astrok_focuser`: absolute focuser (0 to the device's `max_steps`).
- `indi_astrok_rotator`: rotator in degrees, converted with the device's
  `steps_per_degree`.

The mode is set by the jumper on the board; each driver checks it on connect
and refuses a device that started in the other mode.

## Requirements

- INDI **2.0 or later** (`libindi-dev`). Some distributions still ship 1.9.x,
  which these drivers do not build against.
- GLib/GIO (`libglib2.0-dev`) and BlueZ (`bluetoothd` running) for Bluetooth.
- CMake 3.18+ and a C++17 compiler.

## Build and install

```bash
cd indi-driver
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j4
sudo cmake --install build
```

This installs both drivers and `indi_astrok.xml` in `/usr/share/indi`, so INDI
clients list them under Focusers and Rotators, manufacturer AstroK.

## Connecting

Connection tab:

- **Link**: USB or Bluetooth LE.
- **USB port**: the default is `/dev/ttyACM0`. *Find on USB* looks for an
  Espressif USB port with an AstroK in the right mode, and fills in a
  `/dev/serial/by-id/...` path that does not change between reboots. If the
  saved port fails on connect, the driver searches on its own.
- **Bluetooth**: the device address and its 4-digit PIN. *Find on Bluetooth*
  scans for 5 s and lists the AstroK devices in range. If the device has no
  PIN yet, the one typed here becomes its PIN. The PIN can be seen or changed
  over USB (Wireless tab), or with the Windows app.

No Bluetooth pairing is involved; access is controlled by the PIN only.

## What else the drivers do

- **Device**: firmware version, motor and chip, WiFi status, TMC driver status.
- **Motor**: run and hold current, microsteps, speed, acceleration, stall
  settings, and steps per degree for the rotator. Applied at once.
- **Wireless**: turn WiFi and Bluetooth on or off, access point name and
  password, up to 4 networks to join, Bluetooth PIN (USB only). These apply
  after a reboot.
- **Maintenance**: reboot, factory reset (press twice), and firmware update
  from a release file `astrok_<motor>_v<x.y.z>_<yyyymmdd>.bin` (program and
  setup web page). The driver refuses an image for another chip or motor.

Every setting is stored on the device itself, as with the web page and the
Windows app. The INDI configuration file only keeps how to reach the device.

## Running in a container

Bluetooth goes through the host's BlueZ over the system D-Bus. A container
needs `/run/dbus` mounted and the host's `bluetoothd` running; USB needs the
serial device passed through (`--device /dev/ttyACM0`, or `/dev/serial/by-id`
mounted).

## Serial port permissions

The user running `indiserver` must be in the `dialout` group to open
`/dev/ttyACM*`.
