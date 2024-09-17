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
