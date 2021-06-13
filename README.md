# SuperTuxKart
[![Linux build status](https://github.com/supertuxkart/stk-code/actions/workflows/linux.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/linux.yml)
[![Apple build status](https://github.com/supertuxkart/stk-code/actions/workflows/apple.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/apple.yml)
[![Windows build status](https://github.com/supertuxkart/stk-code/actions/workflows/windows.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/windows.yml)
[![Switch build status](https://github.com/supertuxkart/stk-code/actions/workflows/switch.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/switch.yml)
[![#supertuxkart on the libera IRC network](https://img.shields.io/badge/libera-%23supertuxkart-brightgreen.svg)](https://web.libera.chat/?channels=#supertuxkart)

SuperTuxKart is a free kart racing game. It focuses on fun and not on realistic kart physics. Instructions can be found on the in-game help page.

The SuperTuxKart homepage can be found at <https://supertuxkart.net/>. There is also our [FAQ](https://supertuxkart.net/FAQ) and information on how get in touch with the [community](https://supertuxkart.net/Community).

Latest release binaries can be found [here](https://github.com/supertuxkart/stk-code/releases/latest), and preview release [here](https://github.com/supertuxkart/stk-code/releases/preview).

**You are viewing a modified version of STK**, most important changes are listed [here](/FORK_CHANGES.md).

## Hardware Requirements
To run SuperTuxKart, make sure that your computer's specifications are equal or higher than the following specifications:

* A graphics card capable of 3D rendering - NVIDIA GeForce 470 GTX, AMD Radeon 6870 HD series card or Intel HD Graphics 4000 and newer. OpenGL >= 3.3
* You should have a dual-core CPU that's running at 1 GHz or faster.
* You'll need at least 512 MB of free VRAM (video memory).
* System memory: 1 GB
* Minimum disk space: 700 MB
* Ideally, you'll want a joystick with at least 6 buttons.

## License
The software is released under the GNU General Public License (GPL) which can be found in the file [`COPYING`](/COPYING) in the same directory as this file. Information about the licenses for the artwork is contained in `data/licenses`.

---

## 3D coordinates
A reminder for those who are looking at the code and 3D models:

SuperTuxKart: X right, Y up, Z forwards

Blender: X right, Y forwards, Z up

The export utilities  perform the needed transformation, so in Blender you just work with the XY plane as ground, and things will appear fine in STK (using XZ as ground in the code, obviously).

## Building from source

Building instructions can be found in [`INSTALL.md`](/INSTALL.md)

## Info on experimental anti-troll system
If activated, a timer is kept for all players. System can be activated in xml config or in the lobby with command:
\admin troll [0,1]

Timer is increased by the amount of time when
* a player drives in the wrong way
* a player moves below a given speed (troll-max-stop-speed)

Timer is not increased when the player is hit or rescued (during animation).

It is decreased (not below 0 obviously) when a player drives in correct direction with at least given speed (troll-min-normal-speed).

If a player's timer exceeds the warning level (troll-warning-time), they get a warning (troll-warn-message). After that the timer is reset.
If a player got a warning and their timer exceeds the kick level (troll-kick-time), they get kicked.
Warnings and Kicks are logged.

Config can be controlled in xml file like this:

    <!-- Set values for anti-troll system -->
    <!-- If true, use anti troll system
         This can also be controlled with server command:
         \admin troll [0,1] -->
    <use-anti-troll-system value="true" />
    <!-- Warn string to show to player -->
    <troll-warn-message value="WARNING: You troll, you get kicked !!" />
    <!-- Warn player that drives backwards or stopps for given time -->
    <troll-warning-time value="7.0" />
    <!-- Kick player that drives backwards or stopps for given time -->
    <troll-kick-time value="10.0" />
    <!-- Minimum speed in correct direction to decrease wrong-way timer -->
    <troll-min-normal-speed value="12.0" />
    <!-- A player going slower than this is considered stopping -->
    <troll-max-stop-speed value="5.0" />

### Things to note
* The system can be fooled of course, but it should be a lot harder to troll. Testing would be necessary. However, when I set up a test server, most players coming by just wanted to play normally...
* The system should not warn or kick weak players who get lost or stuck on obstacles (too quickly). The current configuration values are most probably not yet perfect.
* Players should probably be informed about the system, because waiting for others (like some pros do when giving weaker players some advice or driving lessons) would be punished, so the system should not be activated in those cases.
* If a player presses UP-key early, the timer is increased during time punishment at the beginning of the race (1 sec). I don't know how to find out if a player is in that state.
* **There may still be bugs** (of course)... ;)
