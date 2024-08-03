#!/usr/bin/env python3

# Usage: update_database_v2_to_v3.py < tables_file
# tables_file should be any file containing all the names
# of *_results tables which you wish to update.
# Names should be on separate lines of the file.
# Run the script, it will print the needed sqlite3 statements
# in the order in which you should apply them.

# Make sure your tables don't have columns ending with `suffix`,
# change the suffix if needed.

names = []
try:
    while True:
        line = input()
        if len(line) > 0:
            names.append(line)
except EOFError:
    pass

suffix = '_old'

def add_column(table, name, description):
    print('ALTER TABLE {} ADD COLUMN {} {};'.format(table, name, description))

def rename_column(table, name1, name2):
    print('ALTER TABLE {} RENAME COLUMN {} TO {};'.format(table, name1, name2))

def copy_values(table, name1, name2):
    print('UPDATE {} SET {} = {};'.format(table, name1, name2))

def drop_column(table, name):
    print('ALTER TABLE {} DROP COLUMN {};'.format(table, name))

def set_default(table, name, new_description, new_default):
    rename_column(table, name, name + suffix)
    add_column(table, name, new_description + " DEFAULT " + new_default)
    copy_values(table, name, name + suffix)
    drop_column(table, name + suffix)

for name in names:
    set_default(name, 'venue', 'TEXT NOT NULL', '""')
    set_default(name, 'reverse', 'TEXT NOT NULL', '""')
    set_default(name, 'mode', 'TEXT NOT NULL', '""')
    rename_column(name, 'laps', 'value_limit')
    set_default(name, 'value_limit', 'INTEGER NOT NULL', '0')
    add_column(name, 'time_limit', 'REAL DEFAULT 0')
    set_default(name, 'difficulty', 'INTEGER', '1')
    set_default(name, 'config', 'TEXT', '""')
    set_default(name, 'items', 'TEXT', '""')
    add_column(name, 'flag_return_timeout', 'INTEGER DEFAULT 0')
    add_column(name, 'flag_deactivated_time', 'INTEGER DEFAULT 0')
    set_default(name, 'username', 'TEXT NOT NULL', '""')
    set_default(name, 'kart', 'TEXT', '""')
    add_column(name, 'kart_class', 'TEXT DEFAULT ""')
    add_column(name, 'team', 'INTEGER DEFAULT -1')
    add_column(name, 'handicap', 'INTEGER DEFAULT -1')
    add_column(name, 'start_pos', 'INTEGER DEFAULT -1')
    add_column(name, 'fastest_lap', 'REAL DEFAULT -1')
    add_column(name, 'sog_time', 'REAL DEFAULT -1')
    add_column(name, 'online_id', 'INTEGER DEFAULT -1')
    add_column(name, 'country_code', 'TEXT DEFAULT ""')
    add_column(name, 'is_autofinish', 'INTEGER DEFAULT 0')
    rename_column(name, 'is_quit', 'is_not_full')
    add_column(name, 'game_duration', 'REAL DEFAULT -1')
    add_column(name, 'when_joined', 'REAL DEFAULT -1')
    add_column(name, 'when_left', 'REAL DEFAULT -1')
    add_column(name, 'game_event', 'INTEGER DEFAULT 0')
    add_column(name, 'other_info', 'TEXT DEFAULT ""')
    print('UPDATE {} SET online_id = 0 WHERE username like "* %";'.format(name))
    print('UPDATE {} SET game_duration = value_limit WHERE mode = "ffa";'.format(name))
    print('UPDATE {} SET value_limit = 0 WHERE mode = "ffa";'.format(name))

