AstroK Rotator - ASCOM driver and setup tool
=================================================

WHAT WAS INSTALLED
------------------
1. The ASCOM driver "AstroK Rotator", for N.I.N.A. and any other
   ASCOM-compatible capture program.
2. "AstroK Rotator" in the Start menu (and on the desktop, if you chose
   it): an application that opens the same setup window without going
   through N.I.N.A. or the ASCOM Chooser. Use it to set up the device on a
   clean laptop, or to change the WiFi without opening the capture program.
   It does not touch the ASCOM profile: everything goes to the device over
   USB.

USING IT IN N.I.N.A.
--------------------
Equipment -> Rotator -> ASCOM -> "AstroK Rotator".
Click the gear to choose the COM port before connecting.

The device must be in rotator mode (set by the mode jumper on the board at
boot). If it is in focuser mode, the driver tells you when connecting.

SETUP WINDOW
------------
From the driver it opens with the gear button, with the driver disconnected.
It has three tabs:

- Rotator: COM port (with automatic device search), connect and
  disconnect, live angle, turns of ±1 / ±10 / ±90 degrees, stop, go to an
  angle, set the current angle to a value (to recover the reference) and the
  steps per degree, which are stored on the device itself. If the angle shown
  does not match the real one, this last value is the one to correct: it
  depends on the gear reduction.
- Advanced: motor current, standstill current, microsteps, reverse
  direction and collision detection (StallGuard). They are applied on save,
  without restarting the device. Factory reset is also here.
- WiFi: current IP, name and password of the device's own access point and
  the list of known networks. Saving restarts the device, because the
  network is only set up at boot.

The window is available in Spanish and English; pick the language at the
bottom.

WIRELESS ALTERNATIVE (ASCOM ALPACA)
-----------------------------------
The device is also offered over WiFi as an ASCOM ALPACA device, with
automatic discovery: N.I.N.A. finds it on its own, without this driver and
without a USB cable. This driver exists to have a reliable wired path when
WiFi is not an option.

REQUIREMENTS
------------
- ASCOM Platform 6 or later (https://ascom-standards.org). Without it, the
  driver cannot be used from capture programs.
- The device connected over USB. Opening the port restarts the device; this
  is normal and it takes a few seconds to answer.

UNINSTALLING
------------
From Windows "Add or remove programs". Uninstalling also removes the
driver's ASCOM registration.
