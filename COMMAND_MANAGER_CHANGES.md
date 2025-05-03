All of this still holds, but remember you are looking at Nomagno's Tyre Mod Edition, which includes but is not limited to kimden's server fork. There are tons of gameplay and network changes apart from what is listed here.

The text below is spoken by kimden, and so words like "I", "my", and "we" refer to him and his repository.

---

This page lists the major changes of this repository compared to standard STK code, as of March 2025. Most changes (sadly, not all of them) here are implemented as options, that is, you can disable them and return to standard behaviour.

You can find more information such as explanations and minor details in [wiki](https://github.com/kimden/stk-code/wiki/). It will be probably filled with even more data in the future.

## Improved Grand Prix mode

* Server doesn't become private
* Players can pick karts, join or leave
* Custom scoring systems
* Custom teams (up to 9)
* Standings ingame (short) and in the lobby (longer), including teams
* Anti-troll and team hit punishment systems, by Heuchi1
* Allows to shuffle the starting grid every race

## Records, replays and stats

* Game results for **all** modes can be saved to the database, together with game settings and kart statistics.
* Public or private record tables can be created using that information
* Ghost replays can be recorded on servers if set if the config and if players agree
* Players who beat server records can be notified (includes separation of times set with different config files)
* Maximum replay size is increased

## Chat

* Private chat with an arbitrary set of players
* Teamchat during CTF and soccer which works both ingame and in the lobby
* Messages are **never** logged, even by the bots (though they pass through the server code)

## Game configuration, available maps and karts, playing restrictions

* Game length can be set to a fixed value, or to a certain multiple of default lap number, or to be chosen ingame
* Game direction can be set to forward or reverse, or to be chosen ingame
* For karts and maps, the next game can be customized so that the set of karts/maps available for choice is fixed, or defined by a certain algorithm (including "you have to play this track", "select from $N$ random maps from this set", "you can choose either Adiumy, or whatever is given to you by random of Suzanne and Konqi")
* The same is true for any number of next games, and the server can be set to repeat the customization every number of games

All of the above settings can be changed arbitrarily when the server already runs, or in the config before the server starts.

* Servers can be started with other powerup.xml or kart_characteristics.xml files being specified
* It's possible for a server to force certain maps for players to join the server or to join the game, or vice versa, disable certain maps (or all but a certain set of maps). There are various commands to find out which addons you (or other players) have or don't have, to make it easy to deal with the above
* The default setting of number of playable slots (playable places on the server) can be changed with a command
* If the player is unable to play due to imposed restrictions (lacking maps, being out of playable slots), that player doesn't affect the set of available maps, and is given the hourglass icon.

## Command manager

Command manager is an entity made to manipulate all existing server commands easier. It supports:

* commands with voting
* text commands / commands reading external files
* auth commands, which allow to authenticate on other sites using STK accounts
* setting permissions for commands depending on server, without recompiling the code
* subcommands and different permissions for them
* fixing errors in arguments (usernames, map names)
* showing available commands depending on permissions
* documentation for all commands via `/help`
* huge number of new commands

All the commands and their descriptions are available in `data/commands.xml`. This file is also used to generate output for `/commands` and `/help`.

## Moderation and permissions

* It's possible to disable kicks attempted by the crowned player
* If a player kicks another player, the fact of the kick can be stored in the database table with reports
* Inactive players can be kicked automatically from the lobby too, not only from the game
* A hammer mode is added: if a player has a password from the config, or if a player is specified in it, they can get a 'hammer' and change server settings and kick players
* There are commands to ban (or unban) a player temporary from a single server by username, as opposed to standard bans which are applied to all servers using the same database, and are usually banning by online id or IP address
* Players are allowed to report something to server owner without reporting other people
* Starting the game can be allowed or forbidden using a command, or before the server start using config

## Game modes and their improvements

These are not implemented as separate modes, but are rather additions to functionality of existing modes:

* **Soccer tournament** mode: for hosting matches of several soccer games. The rules, the number of games and their duration, allowed arenas, tournament teams can be loaded from a config file. Referees have multiple commands to control the game flow
* **Gnu Elimination** mode: every race, the last (or quitting) non-eliminated player gets eliminated, and eliminated players use the same kart chosen in advance. The last standing player wins

Separate changes for modes:

* Soccer games produce a log of all goals / actions that happened (originally made for tournament mode, but can be used without it too), if the score is edited using tournament commands, the game shows it as a message after each goal and at the end. (This is, however, slightly deprecated by the new database features that allow storing all game results.)
* FFA and CTF scores are preserved per username during the game, so that leaving the game is not punished

## Other features

* You can allow only the crowned player to play (speedrun server), or no one at all (chat server)
* Minor `/spectate` changes (you can invoke it ingame, game doesn't start with everyone spectating, you can choose to stay in lobby without watching the game)
* For a configurable server, you can specify which exact difficulties and modes are allowed
* Players can send a message to the server owner (without reporting anyone else), and the server owner can send one-time messages to specific players
* For private servers, players can be whitelisted so that they can enter any password to join
* Different strategies to determine who scored a goal (allowing or forbidding own goals)
* `max-moveable-objects` is changed to 30 to allow tracks like `addon_bowling` to work
* Server can be customized to reset or preserve many settings like game mode, game length, ..., when everyone leaves it
* Server git version and whether it's modified is shown in `/version`
* Server console allows sending arbitrary messages to players
* Supported player categories aka sets of players (for now only displaying their names, and some minor usage)
* Minor additions in StringUtils used in typo fixing and string parsing

## Current and future feature plans

Since 2025, my approach for developing the code in this repository has changed, as I stopped believing I should do everything in the same way as the developers of the main game, and stopped being afraid of merge conflicts. After all, I even submitted a couple big patches into the official repository that way, and it was fine...

Currently, the big restructurization of the code is underway, and one of the biggest and most complicated files in STK, `server_lobby.cpp`, got split into several files so that each file contains methods about the area it corresponds to. It allowed to fix a few hidden bugs, and will allow adding features in a simpler way in the future.

Another big topic is STK network protocol. It is messy and it's spread all over the codebase, while it's often hard to understand what exactly is being sent. That prevents its modification and advanced usage, and it's planned to rework it, so that it at least is understandable (which would also make the other code near the network packets cleaner).

## Better code

The aforementioned `server_lobby.cpp` is heavily used in all server-side forks, and no wonder its size can skyrocket in them, as people want to add many commands. Several frequently used forks have 12.5k and 7.3k lines of code in that file, while the official code has 4.8k lines. This repository has even fewer lines there, but more smaller files — for most features, there are simply much better places and ways to use them!

The code has no proprietary scripts that are hardcoded inside to be unusable by others, no hidden anti-features such as secretly logged private chats, and here we do credit contributors according to what they have done (as in, `git blame` works correctly). Sadly, that's not a frequent offer in the world of STK — but you can make this offer even better by contributing to it!
