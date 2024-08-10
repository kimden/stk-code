# Database specification

Just like in the standard STK repository, we use SQLite3 databases to store some data. However, as a fork, we store more data.

For simplicity, below we'll call the table named `v{sqlite version}_{server name}_XXXXX` as `_XXXXX`. Be careful: in queries below, `(table name)` has to be substituted with your actual table name without brackets.

## Changes in `_stats` table

Standard STK specification of `_stats` table:
```sql
CREATE TABLE IF NOT EXISTS (table name) (
    host_id INTEGER UNSIGNED NOT NULL PRIMARY KEY, -- Unique host id in STKHost of each connection session for a STKPeer
    ip INTEGER UNSIGNED NOT NULL, -- IP decimal of host
    -- This part is present only if ServerConfig::m_ipv6_connection is enabled
    ipv6 TEXT NOT NULL DEFAULT '', -- IPv6 (if exists) in string of host
    -- End of ipv6-only part
    port INTEGER UNSIGNED NOT NULL, -- Port of host
    online_id INTEGER UNSIGNED NOT NULL, -- Online if of the host (0 for offline account)
    username TEXT NOT NULL, -- First player name in the host (if the host has splitscreen player)
    player_num INTEGER UNSIGNED NOT NULL, -- Number of player(s) from the host, more than 1 if it has splitscreen player
    country_code TEXT NULL DEFAULT NULL, -- 2-letter country code of the host
    version TEXT NOT NULL, -- SuperTuxKart version of the host
    os TEXT NOT NULL, -- Operating system of the host
    connected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when connected
    disconnected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when disconnected (saved when disconnected)
    ping INTEGER UNSIGNED NOT NULL DEFAULT 0, -- Ping of the host
    packet_loss INTEGER NOT NULL DEFAULT 0 -- Mean packet loss count from ENet (saved when disconnected)
) WITHOUT ROWID;
```

Fork specification of `_stats` table:
```sql
CREATE TABLE IF NOT EXISTS (table name) (
    host_id INTEGER UNSIGNED NOT NULL PRIMARY KEY, -- Unique host id in STKHost of each connection session for a STKPeer
    ip INTEGER UNSIGNED NOT NULL, -- IP decimal of host
    -- This part is present only if ServerConfig::m_ipv6_connection is enabled
    ipv6 TEXT NOT NULL DEFAULT '', -- IPv6 (if exists) in string of host
    -- End of ipv6-only part
    port INTEGER UNSIGNED NOT NULL, -- Port of host
    online_id INTEGER UNSIGNED NOT NULL, -- Online if of the host (0 for offline account)
    username TEXT NOT NULL, -- First player name in the host (if the host has splitscreen player)
    player_num INTEGER UNSIGNED NOT NULL, -- Number of player(s) from the host, more than 1 if it has splitscreen player
    country_code TEXT NULL DEFAULT NULL, -- 2-letter country code of the host
    version TEXT NOT NULL, -- SuperTuxKart version of the host
    os TEXT NOT NULL, -- Operating system of the host
    connected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when connected
    disconnected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when disconnected (saved when disconnected)
    ping INTEGER UNSIGNED NOT NULL DEFAULT 0, -- Ping of the host
    packet_loss INTEGER NOT NULL DEFAULT 0, -- Mean packet loss count from ENet (saved when disconnected)
    addon_karts_count INTEGER UNSIGNED NOT NULL DEFAULT -1, -- Number of addon karts of the host
    addon_tracks_count INTEGER UNSIGNED NOT NULL DEFAULT -1, -- Number of addon tracks of the host
    addon_arenas_count INTEGER UNSIGNED NOT NULL DEFAULT -1, -- Number of addon arenas of the host
    addon_soccers_count INTEGER UNSIGNED NOT NULL DEFAULT -1 -- Number of addon soccers of the host
) WITHOUT ROWID;
```

As you can see, they differ only by 4 added fields in the latter table. To convert your `_stats` table from the standard format to fork format, you should use the following:

```sql
ALTER TABLE (table name)
ADD COLUMN addon_karts_count INTEGER UNSIGNED NOT NULL DEFAULT -1;
ALTER TABLE (table name)
ADD COLUMN addon_tracks_count INTEGER UNSIGNED NOT NULL DEFAULT -1;
ALTER TABLE (table name)
ADD COLUMN addon_arenas_count INTEGER UNSIGNED NOT NULL DEFAULT -1;
ALTER TABLE (table name)
ADD COLUMN addon_soccers_count INTEGER UNSIGNED NOT NULL DEFAULT -1;
```

The update has to be done **manually**. The fork does not alter existing tables' structure.

Please note that by a mistake, until August 2024 the code of the fork created a **standard** version of the table in case the table did not exist. It is fixed now. Sorry for the inconvenience caused.


## Changes in `_results` table

Standard STK doesn't have any table for saving the results of the games.

This fork was using such tables for many years. Naturally, they evolved. Previous specifications, as well as ways to update them, are listed here.

Before updating, don't forget to **backup** your database in case something goes wrong.
Versions differ very much sometimes, and updates have to be done **manually** unless stated otherwise, and not by STK code. The reason for that is, ~~we are lazy to write automated updaters inside STK code~~ we are afraid to make mistakes in the code and accidentally modify your database while you might not even suspect that. We decided it's better if you are fully aware of what happens. Sorry for the inconvenience caused by that.

Note that in some table specifications, there is `WITHOUT ROWID` keyword, and in some, there isn't. That is probably caused by a mistake while refactoring code. While we don't use ROWID of this table in the code, if you want to change it in your table, it might be better for you to update the table according to this document, and then create a new one with/without `WITHOUT ROWID` and import data from the old table. This might be a good option if you want to reorder the fields, too.

### Version 0. No `_results` table at all

This is the simplest option, if you have no data, you don't have to care how they are stored in the new database. Starting from August 2024, the fork will handle creation of table according to latest Version 3 itself. You only have to launch STK once.

### Version 1. Very simple table

This corresponds to the following specification of the  `_results` table, created on March 4, 2020:

```sql
CREATE TABLE IF NOT EXISTS (table name) (
    time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Timestamp of the result
    username TEXT NOT NULL, -- User who set the result
    venue TEXT NOT NULL, -- Track for a race
    mode INTEGER NOT NULL, -- Racing mode
    laps INTEGER NOT NULL, -- Number of laps
    result REAL NOT NULL -- Elapsed time for a race, possibly with autofinish
) WITHOUT ROWID;
```

### Version 2. More fields

This corresponds to the following specification of the  `_results` table, created throughout 2022 and 2023:

```sql
CREATE TABLE IF NOT EXISTS (table name) (
    time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Timestamp of the result
    username TEXT NOT NULL, -- User who set the result
    venue TEXT NOT NULL, -- Map used in the game
    reverse TEXT NOT NULL, -- Direction
    mode TEXT NOT NULL, -- Game mode
    laps INTEGER NOT NULL, -- Length (number of laps or duration)
    result REAL NOT NULL, -- Elapsed time for a race, possibly with autofinish
    difficulty INTEGER, -- Difficulty
    kart TEXT,
    config TEXT,
    visible INTEGER DEFAULT 1,
    items TEXT,
    kart_color REAL DEFAULT 0,
    is_quit INTEGER DEFAULT 0
);
```

### Updating from Version 1 to Version 2
If you have this version, you can update it to Version 2 in the following way:
```sql
ALTER TABLE (table name) ADD COLUMN reverse TEXT NOT NULL DEFAULT "";
ALTER TABLE (table name) ADD COLUMN difficulty INTEGER DEFAULT -1;
ALTER TABLE (table name) ADD COLUMN kart TEXT DEFAULT NULL;
ALTER TABLE (table name) ADD COLUMN config TEXT DEFAULT NULL;
ALTER TABLE (table name) ADD COLUMN visible INTEGER DEFAULT 1;
ALTER TABLE (table name) ADD COLUMN items TEXT DEFAULT NULL;
ALTER TABLE (table name) ADD COLUMN kart_color REAL DEFAULT 0;
ALTER TABLE (table name) ADD COLUMN is_quit INTEGER DEFAULT 0;
```

Note that for `reverse`, `difficulty`, `kart`, `config`, `items` above there are default values unspecified in Version 2. They won't be used anywhere apart from filling the missing data for existing rows, as insertions always specify the values of those columns. You can change the default to any other value later if you wish using
```sql
ALTER TABLE (table name) ALTER COLUMN (column name) SET DEFAULT (default value);
```

If you have some extra fields from Version 2, you don't have to create them.

### Version 3. Latest

This corresponds to the following specification of the  `_results` table, committed in August 2024:

```sql
CREATE TABLE IF NOT EXISTS (table name) (
    time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Timestamp of the result
    venue TEXT NOT NULL DEFAULT "", -- Map used in the game
    reverse TEXT NOT NULL DEFAULT "", -- Direction / whether randomly placed items are used
    mode TEXT NOT NULL DEFAULT "", -- Game mode
    value_limit INTEGER NOT NULL DEFAULT 0, -- Length (number of laps/goals/points/captures)
    time_limit REAL NOT NULL DEFAULT 0, -- Length (in seconds)
    difficulty INTEGER DEFAULT -1, -- Difficulty used in the game
    config TEXT DEFAULT "", -- Name of kart_characteristics file used (empty for default)
    items TEXT DEFAULT "", -- Name of powerup.xml file used (empty for default)
    flag_return_timeout INTEGER DEFAULT 0, -- For CTF, the time after which the flag returns to the base itself
    flag_deactivated_time INTEGER DEFAULT 0, -- For CTF, the deactivation time of the flag
    visible INTEGER DEFAULT 1, -- A mark you can use to disable some entries
    username TEXT NOT NULL DEFAULT "", -- User who set the result
    result REAL NOT NULL, -- The result of the game (e.g. elapsed time for a race)
    kart TEXT DEFAULT "", -- Kart used by the player
    kart_class TEXT DEFAULT "", -- Kart class used by a player
    kart_color REAL DEFAULT 0, -- Kart color used by the player
    team INTEGER DEFAULT -1, -- The team of the player
    handicap INTEGER DEFAULT -1, -- Handicap used by the player
    start_pos INTEGER DEFAULT -1, -- Starting position of a player, if applicable
    fastest_lap REAL DEFAULT -1, -- For racing modes, the fastest lap of the player
    sog_time REAL DEFAULT -1, -- For racing players, time until crossing the start line; for soccer/ctf goals, the time since previous goal
    online_id INTEGER DEFAULT -1, -- Online id of a player, if exists
    country_code TEXT DEFAULT "", -- Country code of a player, if determined
    is_autofinish INTEGER DEFAULT 0, -- Whether the player's game was autofinished
    is_not_full INTEGER DEFAULT 0, -- Whether the player quit the game
    game_duration REAL DEFAULT -1, -- Game duration, if known
    when_joined REAL DEFAULT -1, -- The moment when the player joined the game
    when_left REAL DEFAULT -1, -- The moment when the player left the game
    game_event INTEGER DEFAULT 0, -- Whether this row corresponds to a game event (goal, capture, ...)
    other_info TEXT DEFAULT "" -- Any other info that can be useful for the database
);
```

This table is supposed to incorporate the results of **all** online game modes: normal race, time trial, soccer, FFA, CTF.

Just in case you need it, most fields accept NULL values. The only ones that don't are those used in Version 1, and also `time-limit` to be similar to `value-limit`, which existed in Version 1 as `laps`. All the default values for the new columns are not NULL, and most of them correspond either to an "unknown" value which cannot be inserted using latest code, or to a "default" value which can be assumed for the old data too.

### Updating from Version 2 to Version 3

Most of old columns kept their names.

`laps` column was renamed to `value_limit`, as non-racing modes don't have laps, but all modes can be described as a game until a certain number of laps/points/goals/captures (which corresponds to `value_limit`), and/or until a certain time passes (which corresponds to `time_limit`).

`is_quit` column was renamed to `is_not_full`, as in non-racing modes (and probably soon in racing too) it's possible to leave and join the game at any time, and so the value in this column shows whether a player wasn't playing the whole game.

[This script](../../tools/update_database_v2_to_v3.py) allows you to generate the SQL statements for all of your tables. The generated SQL statements will rename 2 columns, add 17 columns, and modify default value for 9 columns (by creating a duplicate column with a new default value). They will also do a few other changes, described in the next section, so that the database suits the new fields completely. The statements generated by the script were tested on a real database, but please **backup** the database anyway.

To run the script, you'll have to **delete** beforehand and **recreate** afterwards all your indices and views related to `_results` tables. (You would probably want to customize them in any case, as there are new fields). To do that, first run the following query:
```sql
select type, name, sql from sqlite_master where type in ("index", "view") and name like "%_results%";
```
Save its output. For each `name`, run `DROP VIEW name;` or `DROP INDEX name;` depending on whether type is `view` or `index`. Then you can run the script and execute the statements produced by it. After that, you can use `sql` values as statements to recreate the views and indices (keep in mind that two fields changed their names).

Sorry for the inconvenience.


### Explanation of columns of Version 3, and its comparison to older versions

Will be updated later.
