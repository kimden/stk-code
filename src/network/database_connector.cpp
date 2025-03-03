//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2024 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifdef ENABLE_SQLITE3

#include "network/database_connector.hpp"

#include "network/network_player_profile.hpp"
#include "network/server_config.hpp"
#include "network/socket_address.hpp"
#include "network/stk_host.hpp"
#include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
#include "utils/game_info.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

//-----------------------------------------------------------------------------
/** Prints "?" to the output stream and saves the Binder object to the
 *   corresponding BinderCollection so that it can produce bind function later
 *   When we invoke StringUtils::insertValues with a Binder argument, the
 *   implementation of insertValues ensures that this function is invoked for
 *   all Binder arguments from left to right.
 */
std::ostream& operator << (std::ostream& os, const Binder& binder)
{
    os << "?";
    binder.m_collection.lock()->m_binders.emplace_back(std::make_shared<Binder>(binder));
    return os;
}   // operator << (Binder)

//-----------------------------------------------------------------------------
/** Returns a bind function that should be used inside an easySQLQuery. As the
 *   Binder objects are already ordered in a correct way, the indices just go
 *   from 1 upwards. Depending on a particular Binder, we can also bind NULL
 *   instead of a string.
 */
std::function<void(sqlite3_stmt* stmt)> BinderCollection::getBindFunction() const
{
    auto binders = m_binders;
    return [binders](sqlite3_stmt* stmt)
    {
        int idx = 1;
        for (std::shared_ptr<Binder> binder: binders)
        {
            if (binder)
            {
                // SQLITE_TRANSIENT to copy string
                if (binder->m_use_null_if_empty && binder->m_value.empty())
                {
                    if (sqlite3_bind_null(stmt, idx) != SQLITE_OK)
                    {
                        Log::error("easySQLQuery", "Failed to bind NULL for %s.",
                            binder->m_name.c_str());
                    }
                }
                else
                {
                    if (sqlite3_bind_text(stmt, idx, binder->m_value.c_str(),
                        -1, SQLITE_TRANSIENT) != SQLITE_OK)
                    {
                        Log::error("easySQLQuery", "Failed to bind %s as %s.",
                            binder->m_value.c_str(), binder->m_name.c_str());
                    }
                }
            }
            ++idx;
        }
    };
}   // BinderCollection::getBindFunction

//-----------------------------------------------------------------------------
/** Opens the database, sets its busy handler and variables related to it. */
void DatabaseConnector::initDatabase()
{
    m_last_poll_db_time = StkTime::getMonoTimeMs();
    m_db = NULL;
    m_ip_ban_table_exists = false;
    m_ipv6_ban_table_exists = false;
    m_online_id_ban_table_exists = false;
    m_ip_geolocation_table_exists = false;
    m_ipv6_geolocation_table_exists = false;
    m_player_reports_table_exists = false;
    if (!ServerConfig::m_sql_management)
        return;
    const std::string& path = ServerConfig::getConfigDirectory() + "/" +
        ServerConfig::m_database_file.c_str();
    int ret = sqlite3_open_v2(path.c_str(), &m_db,
        SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK)
    {
        Log::error("ServerLobby", "Cannot open database: %s.",
            sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = NULL;
        return;
    }
    sqlite3_busy_handler(m_db, [](void* data, int retry)
        {
            int retry_count = ServerConfig::m_database_timeout / 100;
            if (retry < retry_count)
            {
                sqlite3_sleep(100);
                // Return non-zero to let caller retry again
                return 1;
            }
            // Return zero to let caller return SQLITE_BUSY immediately
            return 0;
        }, NULL);
    sqlite3_create_function(m_db, "insideIPv6CIDR", 2, SQLITE_UTF8, NULL,
        &insideIPv6CIDRSQL, NULL, NULL);
    sqlite3_create_function(m_db, "upperIPv6", 1, SQLITE_UTF8, NULL,
        &upperIPv6SQL, NULL, NULL);
    checkTableExists(ServerConfig::m_ip_ban_table, m_ip_ban_table_exists);
    checkTableExists(ServerConfig::m_ipv6_ban_table, m_ipv6_ban_table_exists);
    checkTableExists(ServerConfig::m_online_id_ban_table,
        m_online_id_ban_table_exists);
    checkTableExists(ServerConfig::m_player_reports_table,
        m_player_reports_table_exists);
    checkTableExists(ServerConfig::m_ip_geolocation_table,
        m_ip_geolocation_table_exists);
    checkTableExists(ServerConfig::m_ipv6_geolocation_table,
        m_ipv6_geolocation_table_exists);
    checkTableExists(ServerConfig::m_records_table_name,
        m_records_table_exists, true);
}   // initDatabase

//-----------------------------------------------------------------------------
/** Closes the database. */
void DatabaseConnector::destroyDatabase()
{
    auto peers = STKHost::get()->getPeers();
    for (auto& peer : peers)
        writeDisconnectInfoTable(peer);
    if (m_db != NULL)
        sqlite3_close(m_db);
}   // destroyDatabase

//-----------------------------------------------------------------------------
/** Runs simple query with optional bind function. If output vector pointer is
 *   not (default) nullptr, then the output is written there.
 *  \param query The SQL query with '?'-placeholders for values to bind.
 *  \param output The 2D vector for output rows. If nullptr, the query output
 *                is ignored.
 *  \param bind_function The function for binding missing values.
 *  \return True if no error occurs.
 */
bool DatabaseConnector::easySQLQuery(
       const std::string& query, std::vector<std::vector<std::string>>* output,
                         std::function<void(sqlite3_stmt* stmt)> bind_function,
                                                  std::string null_value) const
{
    if (!m_db)
        return false;
    sqlite3_stmt* stmt = NULL;
    int ret = sqlite3_prepare_v2(m_db, query.c_str(), -1, &stmt, 0);
    if (ret == SQLITE_OK)
    {
        if (bind_function)
            bind_function(stmt);
        ret = sqlite3_step(stmt);
        if (output)
        {
            output->clear();
            while (ret == SQLITE_ROW)
            {
                output->emplace_back();
                int columns = sqlite3_column_count(stmt);
                for (int i = 0; i < columns; ++i)
                {
                    const char* value = (char*)sqlite3_column_text(stmt, i);
                    if (value == nullptr)
                        output->back().push_back(null_value);
                    else
                        output->back().push_back(std::string(value));
                }
                ret = sqlite3_step(stmt);
            }
        }
        ret = sqlite3_finalize(stmt);
        if (ret != SQLITE_OK)
        {
            Log::error("DatabaseConnector",
                "Error finalize database for easy query %s: %s",
                query.c_str(), sqlite3_errmsg(m_db));
            return false;
        }
    }
    else
    {
        Log::error("DatabaseConnector",
            "Error preparing database for easy query %s: %s",
            query.c_str(), sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}   // easySQLQuery

//-----------------------------------------------------------------------------
/** Performs a query to determine if a certain table exists.
 *  \param table The searched name.
 *  \param result The output value.
 *  \param allow_views True if views are allowed too (in case we only use SELECT).
 */
void DatabaseConnector::checkTableExists(const std::string& table, bool& result, bool allow_views)
{
    if (!m_db)
        return;
    result = false;
    if (!table.empty())
    {
        std::string query = "SELECT count(type) FROM sqlite_master "
            "WHERE (type='table'";
        if (allow_views)
            query += " OR type='view'";
        query += ") AND name='%s';";
        query = StringUtils::insertValues(query, table.c_str());

        std::vector<std::vector<std::string>> output;
        if (easySQLQuery(query, &output) && !output.empty())
        {
            int number;
            if (StringUtils::fromString(output[0][0], number) && number == 1)
            {
                Log::info("DatabaseConnector", "Table named %s will be used.",
                    table.c_str());
                result = true;
            }
        }
    }
    if (!result && !table.empty())
    {
        Log::warn("DatabaseConnector", "Table named %s not found in database.",
            table.c_str());
    }
}   // checkTableExists

//-----------------------------------------------------------------------------
/** Queries the database's IP mapping to determine the country code for an
 *   address.
 *  \param addr Queried address.
 *  \return A country code string if the address is found in the mapping,
 *          and an empty string otherwise.
 */
std::string DatabaseConnector::ip2Country(const SocketAddress& addr) const
{
    if (!m_db || !m_ip_geolocation_table_exists || addr.isLAN())
        return "";

    std::string cc_code;
    std::string query = StringUtils::insertValues(
        "SELECT country_code FROM %s "
        "WHERE `ip_start` <= %d AND `ip_end` >= %d "
        "ORDER BY `ip_start` DESC LIMIT 1;",
        ServerConfig::m_ip_geolocation_table.c_str(), addr.getIP(),
        addr.getIP());

    std::vector<std::vector<std::string>> output;
    if (easySQLQuery(query, &output) && !output.empty())
    {
        cc_code = output[0][0];
    }
    return cc_code;
}   // ip2Country

//-----------------------------------------------------------------------------
/** Queries the database's IPv6 mapping to determine the country code for an
 *   address.
 *  \param addr Queried address.
 *  \return A country code string if the address is found in the mapping,
 *          and an empty string otherwise.
 */
std::string DatabaseConnector::ipv62Country(const SocketAddress& addr) const
{
    if (!m_db || !m_ipv6_geolocation_table_exists)
        return "";

    std::string cc_code;
    const std::string& ipv6 = addr.toString(false/*show_port*/);
    std::string query = StringUtils::insertValues(
        "SELECT country_code FROM %s "
        "WHERE `ip_start` <= upperIPv6(\"%s\") AND `ip_end` >= upperIPv6(\"%s\") "
        "ORDER BY `ip_start` DESC LIMIT 1;",
        ServerConfig::m_ipv6_geolocation_table.c_str(), ipv6.c_str(),
        ipv6.c_str());

    std::vector<std::vector<std::string>> output;
    if (easySQLQuery(query, &output) && !output.empty())
    {
        cc_code = output[0][0];
    }
    return cc_code;
}   // ipv62Country

// ----------------------------------------------------------------------------
/** A function invoked within SQLite */
void DatabaseConnector::upperIPv6SQL(sqlite3_context* context, int argc,
                         sqlite3_value** argv)
{
    if (argc != 1)
    {
        sqlite3_result_int64(context, 0);
        return;
    }

    char* ipv6 = (char*)sqlite3_value_text(argv[0]);
    if (ipv6 == NULL)
    {
        sqlite3_result_int64(context, 0);
        return;
    }
    sqlite3_result_int64(context, upperIPv6(ipv6));
}

// ----------------------------------------------------------------------------
/** A function that checks within SQLite whether an IPv6 address (argv[1])
 *   is located within a specified block (argv[0]) of IPv6 addresses.
 */
void DatabaseConnector::insideIPv6CIDRSQL(sqlite3_context* context, int argc,
                       sqlite3_value** argv)
{
    if (argc != 2)
    {
        sqlite3_result_int(context, 0);
        return;
    }

    char* ipv6_cidr = (char*)sqlite3_value_text(argv[0]);
    char* ipv6_in = (char*)sqlite3_value_text(argv[1]);
    if (ipv6_cidr == NULL || ipv6_in == NULL)
    {
        sqlite3_result_int(context, 0);
        return;
    }
    sqlite3_result_int(context, insideIPv6CIDR(ipv6_cidr, ipv6_in));
}   // insideIPv6CIDRSQL

// ----------------------------------------------------------------------------
/*
Copy below code so it can be use as loadable extension to be used in sqlite3
command interface (together with andIPv6 and insideIPv6CIDR from stk_ipv6)

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
// ----------------------------------------------------------------------------
sqlite3_extension_init(sqlite3* db, char** pzErrMsg,
                       const sqlite3_api_routines* pApi)
{
    SQLITE_EXTENSION_INIT2(pApi)
    sqlite3_create_function(db, "insideIPv6CIDR", 2, SQLITE_UTF8, NULL,
        insideIPv6CIDRSQL, NULL, NULL);
    sqlite3_create_function(db, "upperIPv6", 1, SQLITE_UTF8,  0, upperIPv6SQL,
        0, 0);
    return 0;
}   // sqlite3_extension_init
*/

//-----------------------------------------------------------------------------
/** When a peer disconnects from the server, this function saves to the
 *   database peer's disconnection time and statistics (ping and packet loss).
 *  \param peer Disconnecting peer.
 */
void DatabaseConnector::writeDisconnectInfoTable(std::shared_ptr<STKPeer> peer)
{
    if (m_server_stats_table.empty())
        return;
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET disconnected_time = datetime('now'), "
        "ping = %d, packet_loss = %d "
        "WHERE host_id = %u;", m_server_stats_table.c_str(),
        peer->getAveragePing(), peer->getPacketLoss(),
        peer->getHostId());
    easySQLQuery(query);
}   // writeDisconnectInfoTable

//-----------------------------------------------------------------------------
/** Creates necessary tables and views if they don't exist yet in the database.
 *   As the function is invoked during the server launch, it also updates rows
 *   related to players whose disconnection time wasn't written, and loads
 *   last used host id.
 */
void DatabaseConnector::initServerStatsTable()
{
    if (!ServerConfig::m_sql_management || !m_db)
        return;
    std::string table_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_stats";

    std::ostringstream oss;
    oss << "CREATE TABLE IF NOT EXISTS " << table_name << " (\n"
        "    host_id INTEGER UNSIGNED NOT NULL PRIMARY KEY, -- Unique host id in STKHost of each connection session for a STKPeer\n"
        "    ip INTEGER UNSIGNED NOT NULL, -- IP decimal of host\n";
    if (ServerConfig::m_ipv6_connection)
        oss << "    ipv6 TEXT NOT NULL DEFAULT '', -- IPv6 (if exists) in string of host\n";
    oss << "    port INTEGER UNSIGNED NOT NULL, -- Port of host\n"
        "    online_id INTEGER UNSIGNED NOT NULL, -- Online if of the host (0 for offline account)\n"
        "    username TEXT NOT NULL, -- First player name in the host (if the host has splitscreen player)\n"
        "    player_num INTEGER UNSIGNED NOT NULL, -- Number of player(s) from the host, more than 1 if it has splitscreen player\n"
        "    country_code TEXT NULL DEFAULT NULL, -- 2-letter country code of the host\n"
        "    version TEXT NOT NULL, -- SuperTuxKart version of the host\n"
        "    os TEXT NOT NULL, -- Operating system of the host\n"
        "    connected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when connected\n"
        "    disconnected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when disconnected (saved when disconnected)\n"
        "    ping INTEGER UNSIGNED NOT NULL DEFAULT 0, -- Ping of the host\n"
        "    packet_loss INTEGER NOT NULL DEFAULT 0, -- Mean packet loss count from ENet (saved when disconnected)\n"
        "    addon_karts_count INTEGER UNSIGNED NOT NULL DEFAULT -1, -- Number of addon karts of the host\n"
        "    addon_tracks_count INTEGER UNSIGNED NOT NULL DEFAULT -1, -- Number of addon tracks of the host\n"
        "    addon_arenas_count INTEGER UNSIGNED NOT NULL DEFAULT -1, -- Number of addon arenas of the host\n"
        "    addon_soccers_count INTEGER UNSIGNED NOT NULL DEFAULT -1 -- Number of addon soccers of the host\n"
        ") WITHOUT ROWID;";
    std::string query = oss.str();

    if (easySQLQuery(query))
        m_server_stats_table = table_name;

    if (m_server_stats_table.empty())
        return;

    // Extra default table _countries:
    // Server owner need to initialise this table himself, check NETWORKING.md
    std::string country_table_name = std::string("v") + StringUtils::toString(
        ServerConfig::m_server_db_version) + "_countries";
    query = StringUtils::insertValues(
        "CREATE TABLE IF NOT EXISTS %s (\n"
        "    country_code TEXT NOT NULL PRIMARY KEY UNIQUE, -- Unique 2-letter country code\n"
        "    country_flag TEXT NOT NULL, -- Unicode country flag representation of 2-letter country code\n"
        "    country_name TEXT NOT NULL -- Readable name of this country\n"
        ") WITHOUT ROWID;", country_table_name.c_str());
    easySQLQuery(query);

    std::vector<std::vector<std::string>> output;

    if (ServerConfig::m_store_results)
    {
        // Extra default table _results:
        // Server owner need to initialise this table himself, check NETWORKING.md
        m_results_table_name = std::string("v") + StringUtils::toString(
            ServerConfig::m_server_db_version) + "_" +
            ServerConfig::m_server_uid + "_results";
        query = StringUtils::insertValues(
            "CREATE TABLE IF NOT EXISTS %s (\n"
            // Columns describing game settings
            "    time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Timestamp of the result\n"
            "    venue TEXT NOT NULL DEFAULT \"\", -- Map used in the game\n"
            "    reverse TEXT NOT NULL DEFAULT \"\", -- Direction / whether randomly placed items are used\n"
            "    mode TEXT NOT NULL DEFAULT \"\", -- Game mode\n"
            "    value_limit INTEGER NOT NULL DEFAULT 0, -- Length (number of laps/goals/points/captures)\n"
            "    time_limit REAL NOT NULL DEFAULT 0, -- Length (in seconds)\n"
            "    difficulty INTEGER DEFAULT -1, -- Difficulty used in the game\n"
            "    config TEXT DEFAULT \"\", -- Name of kart_characteristics file used (empty for default)\n"
            "    items TEXT DEFAULT \"\", -- Name of powerup.xml file used (empty for default)\n"
            "    flag_return_timeout INTEGER DEFAULT 0, -- For CTF, the time after which the flag returns to the base itself\n"
            "    flag_deactivated_time INTEGER DEFAULT 0, -- For CTF, the deactivation time of the flag\n"
            "    visible INTEGER DEFAULT 1, -- A mark you can use to disable some entries\n"
            // Columns describing achieved results
            "    username TEXT NOT NULL DEFAULT \"\", -- User who set the result\n"
            "    result REAL NOT NULL, -- The result of the game (e.g. elapsed time for a race)\n"
            "    kart TEXT DEFAULT \"\", -- Kart used by the player\n"
            "    kart_class TEXT DEFAULT \"\", -- Kart class used by a player\n"
            "    kart_color REAL DEFAULT 0, -- Kart color used by the player\n"
            "    team INTEGER DEFAULT -1, -- The team of the player\n"
            "    handicap INTEGER DEFAULT -1, -- Handicap used by the player\n"
            "    start_pos INTEGER DEFAULT -1, -- Starting position of a player, if applicable\n"
            "    fastest_lap REAL DEFAULT -1, -- For racing modes, the fastest lap of the player\n"
            "    sog_time REAL DEFAULT -1, -- For racing players, time until crossing the start line; for soccer/ctf goals, the time since previous goal\n"
            "    online_id INTEGER DEFAULT -1, -- Online id of a player, if exists\n"
            "    country_code TEXT DEFAULT \"\", -- Country code of a player, if determined\n"
            "    is_autofinish INTEGER DEFAULT 0, -- Whether the player's game was autofinished\n"
            "    is_not_full INTEGER DEFAULT 0, -- Whether the player quit the game\n"
            "    game_duration REAL DEFAULT -1, -- Game duration, if known\n"
            "    when_joined REAL DEFAULT -1, -- The moment when the player joined the game\n"
            "    when_left REAL DEFAULT -1, -- The moment when the player left the game\n"
            "    game_event INTEGER DEFAULT 0, -- Whether this row corresponds to a game event (goal, capture, ...)\n"
            "    other_info TEXT DEFAULT \"\" -- Any other info that can be useful for the database\n"
            ");", m_results_table_name.c_str());
        easySQLQuery(query);

        // If the results table after that query has too few columns,
        // warn about database update
        output.clear();
        query = "SELECT count(cid) FROM pragma_table_info('" + m_results_table_name + "');";
        int columns = -1;
        if (easySQLQuery(query, &output) && !output.empty() && !output[0].empty())
        {
            if (!StringUtils::fromString(output[0][0], columns))
                columns = -1;
        }
        if (columns < 31) // the number of columns at the moment of big update
        {
#ifdef ENABLE_FATAL_WHEN_OLD_RECORDS
            Log::fatal("DatabaseConnector", ""
                "Your database schema appears to be old. The current code might work incorrectly. "
                "You can update your existing database using guides in docs/fork folder. "
                "Or you can use a new database for the meantime, while updating your old database later. "
                "Or you can suppress this warning by passing -DFATAL_WHEN_OLD_RECORDS=OFF to CMake. "
                "Or you can set store-results to false in the config, then make "
                "sure your records-table-name is either empty or the table has enough columns. "
                "We apologize for inconvenience. If you have questions, we'd be glad to resolve them. "
                "Please get in touch with us using kimden/stk-code repo, or in any other way."
                "\nAborting the server."
            );
#else
            Log::warn("DatabaseConnector", ""
                "Your database schema appears to be old. The current code might work incorrectly. "
                "You suppressed this warning, so nothing happens. "
                "You can make this warning abort STK by passing -DFATAL_WHEN_OLD_RECORDS=OFF to CMake. "
            );
#endif
        }
    }

    // Default views:
    // _full_stats
    // Full stats with ip in human readable format and time played of each
    // players in minutes
    std::string full_stats_view_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_full_stats";
    oss.str("");
    oss << "CREATE VIEW IF NOT EXISTS " << full_stats_view_name << " AS\n"
        << "    SELECT host_id, ip,\n"
        << "    ((ip >> 24) & 255) ||'.'|| ((ip >> 16) & 255) ||'.'|| ((ip >>  8) & 255) ||'.'|| ((ip ) & 255) AS ip_readable,\n";
    if (ServerConfig::m_ipv6_connection)
        oss << "    ipv6,";
    oss << "    port, online_id, username, player_num,\n"
        << "    " << m_server_stats_table << ".country_code AS country_code, country_flag, country_name, version, os,\n"
        << "    ROUND((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0, 2) AS time_played,\n"
        << "    connected_time, disconnected_time, ping, packet_loss FROM " << m_server_stats_table << "\n"
        << "    LEFT JOIN " << country_table_name << " ON "
        <<      country_table_name << ".country_code = " << m_server_stats_table << ".country_code\n"
        << "    ORDER BY connected_time DESC;";
    query = oss.str();
    easySQLQuery(query);

    // _current_players
    // Current players in server with ip in human readable format and time
    // played of each players in minutes
    std::string current_players_view_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_current_players";
    oss.str("");
    oss.clear();
    oss << "CREATE VIEW IF NOT EXISTS " << current_players_view_name << " AS\n"
        << "    SELECT host_id, ip,\n"
        << "    ((ip >> 24) & 255) ||'.'|| ((ip >> 16) & 255) ||'.'|| ((ip >>  8) & 255) ||'.'|| ((ip ) & 255) AS ip_readable,\n";
    if (ServerConfig::m_ipv6_connection)
        oss << "    ipv6,";
    oss << "    port, online_id, username, player_num,\n"
        << "    " << m_server_stats_table << ".country_code AS country_code, country_flag, country_name, version, os,\n"
        << "    ROUND((STRFTIME(\"%s\", 'now') - STRFTIME(\"%s\", connected_time)) / 60.0, 2) AS time_played,\n"
        << "    connected_time, ping FROM " << m_server_stats_table << "\n"
        << "    LEFT JOIN " << country_table_name << " ON "
        <<      country_table_name << ".country_code = " << m_server_stats_table << ".country_code\n"
        << "    WHERE connected_time = disconnected_time;";
    query = oss.str();
    easySQLQuery(query);

    // _player_stats
    // All players with online id and username with their time played stats
    // in this server since creation of this database
    // If sqlite supports window functions (since 3.25), it will include last session player info (ip, country, ping...)
    std::string player_stats_view_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_player_stats";
    oss.str("");
    oss.clear();
    if (sqlite3_libversion_number() < 3025000)
    {
        oss << "CREATE VIEW IF NOT EXISTS " << player_stats_view_name << " AS\n"
            << "    SELECT online_id, username, COUNT(online_id) AS num_connections,\n"
            << "    MIN(connected_time) AS first_connected_time,\n"
            << "    MAX(connected_time) AS last_connected_time,\n"
            << "    ROUND(SUM((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS total_time_played,\n"
            << "    ROUND(AVG((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS average_time_played,\n"
            << "    ROUND(MIN((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS min_time_played,\n"
            << "    ROUND(MAX((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS max_time_played\n"
            << "    FROM " << m_server_stats_table << "\n"
            << "    WHERE online_id != 0 GROUP BY online_id ORDER BY num_connections DESC;";
    }
    else
    {
        oss << "CREATE VIEW IF NOT EXISTS " << player_stats_view_name << " AS\n"
            << "    SELECT a.online_id, a.username, a.ip, a.ip_readable,\n";
        if (ServerConfig::m_ipv6_connection)
            oss << "    a.ipv6,";
        oss << "    a.port, a.player_num,\n"
            << "    a.country_code, a.country_flag, a.country_name, a.version, a.os, a.ping, a.packet_loss,\n"
            << "    b.num_connections, b.first_connected_time, b.first_disconnected_time,\n"
            << "    a.connected_time AS last_connected_time, a.disconnected_time AS last_disconnected_time,\n"
            << "    a.time_played AS last_time_played, b.total_time_played, b.average_time_played,\n"
            << "    b.min_time_played, b.max_time_played\n"
            << "    FROM\n"
            << "    (\n"
            << "        SELECT *,\n"
            << "        ROW_NUMBER() OVER\n"
            << "        (\n"
            << "            PARTITION BY online_id\n"
            << "            ORDER BY connected_time DESC\n"
            << "        ) RowNum\n"
            << "        FROM " << full_stats_view_name << " where online_id != 0\n"
            << "    ) as a\n"
            << "    JOIN\n"
            << "    (\n"
            << "        SELECT online_id, COUNT(online_id) AS num_connections,\n"
            << "        MIN(connected_time) AS first_connected_time,\n"
            << "        MIN(disconnected_time) AS first_disconnected_time,\n"
            << "        ROUND(SUM((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS total_time_played,\n"
            << "        ROUND(AVG((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS average_time_played,\n"
            << "        ROUND(MIN((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS min_time_played,\n"
            << "        ROUND(MAX((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS max_time_played\n"
            << "        FROM " << m_server_stats_table << " WHERE online_id != 0 GROUP BY online_id\n"
            << "    ) AS b\n"
            << "    ON b.online_id = a.online_id\n"
            << "    WHERE RowNum = 1 ORDER BY num_connections DESC;\n";
    }
    query = oss.str();
    easySQLQuery(query);

    uint32_t last_host_id = 0;
    query = StringUtils::insertValues("SELECT MAX(host_id) FROM %s;",
        m_server_stats_table.c_str());

    output.clear();
    if (easySQLQuery(query, &output))
    {
        if (!output.empty() && !output[0].empty()
                && StringUtils::fromString(output[0][0], last_host_id))
        {
            Log::info("DatabaseConnector", "%u was last server session max host id.",
                last_host_id);
        }
    }
    else
    {
        m_server_stats_table = "";
    }

    STKHost::get()->setNextHostId(last_host_id);

    // Update disconnected time (if stk crashed it will not be written)
    query = StringUtils::insertValues(
        "UPDATE %s SET disconnected_time = datetime('now') "
        "WHERE connected_time = disconnected_time;",
        m_server_stats_table.c_str());
    easySQLQuery(query);
}   // initServerStatsTable

//-----------------------------------------------------------------------------
/** Writes a report of one player about another player.
 *  \param reporter Peer that sends the report.
 *  \param reporter_npp Player profile that sends the report.
 *  \param reporting Peer that is reported.
 *  \param reporting_npp Player profile that is reported.
 *  \param info The report message.
 *  \return True if the database query succeeded.
 */
bool DatabaseConnector::writeReport(
       std::shared_ptr<STKPeer> reporter, std::shared_ptr<NetworkPlayerProfile> reporter_npp,
       std::shared_ptr<STKPeer> reporting, std::shared_ptr<NetworkPlayerProfile> reporting_npp,
       irr::core::stringw& info)
{
    std::string query;

    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    if (ServerConfig::m_ipv6_connection)
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(server_uid, reporter_ip, reporter_ipv6, reporter_online_id, reporter_username, "
            "info, reporting_ip, reporting_ipv6, reporting_online_id, reporting_username) "
            "VALUES (%s, %u, \"%s\", %u, %s, %s, %u, \"%s\", %u, %s);",
            ServerConfig::m_player_reports_table.c_str(),
            Binder(coll, ServerConfig::m_server_uid, "server_uid"),
            !reporter->getAddress().isIPv6() ? reporter->getAddress().getIP() : 0,
            reporter->getAddress().isIPv6() ? reporter->getAddress().toString(false) : "",
            reporter_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporter_npp->getName()), "reporter_name"),
            Binder(coll, StringUtils::wideToUtf8(info), "info"),
            !reporting->getAddress().isIPv6() ? reporting->getAddress().getIP() : 0,
            reporting->getAddress().isIPv6() ? reporting->getAddress().toString(false) : "",
            reporting_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporting_npp->getName()), "reporting_name")
        );
    }
    else
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(server_uid, reporter_ip, reporter_online_id, reporter_username, "
            "info, reporting_ip, reporting_online_id, reporting_username) "
            "VALUES (%s, %u, %u, %s, %s, %u, %u, %s);",
            ServerConfig::m_player_reports_table.c_str(),
            Binder(coll, ServerConfig::m_server_uid, "server_uid"),
            reporter->getAddress().getIP(),
            reporter_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporter_npp->getName()), "reporter_name"),
            Binder(coll, StringUtils::wideToUtf8(info), "info"),
            reporting->getAddress().getIP(),
            reporting_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporting_npp->getName()), "reporting_name")
        );
    }
    return easySQLQuery(query, nullptr, coll->getBindFunction());
}   // writeReport

//-----------------------------------------------------------------------------
/** Gets the rows from IPv4 ban table, either all of them (for polling
 *   purposes), or those describing a certain address (if only one peer has to
 *   be checked).
 *  \param ip The IP address to check the database for. If zero, all rows
 *            will be given.
 *  \return A vector of rows in the form of IpBanTableData structures.
 */
std::vector<DatabaseConnector::IpBanTableData>
DatabaseConnector::getIpBanTableData(uint32_t ip) const
{
    std::vector<IpBanTableData> result;
    if (!m_ip_ban_table_exists)
    {
        return result;
    }
    bool single_ip = (ip != 0);
    std::ostringstream oss;
    oss << "SELECT rowid, ip_start, ip_end, reason, description FROM ";
    oss << (std::string)ServerConfig::m_ip_ban_table << " WHERE ";
    if (single_ip)
        oss << "ip_start <= " << ip << " AND ip_end >= " << ip << " AND ";
    oss << "datetime('now') > datetime(starting_time) AND "
        "(expired_days is NULL OR datetime"
        "(starting_time, '+'||expired_days||' days') > datetime('now'))";
    if (single_ip)
        oss << " LIMIT 1";
    oss << ";";
    std::string query = oss.str();

    std::vector<std::vector<std::string>> output;
    easySQLQuery(query, &output);

    for (std::vector<std::string>& row: output)
    {
        IpBanTableData element;
        if (!StringUtils::fromString(row[0], element.row_id))
            continue;
        if (!StringUtils::fromString(row[1], element.ip_start))
            continue;
        if (!StringUtils::fromString(row[2], element.ip_end))
            continue;
        element.reason = row[3];
        element.description = row[4];
        result.push_back(element);
    }
    return result;
}   // getIpBanTableData

//-----------------------------------------------------------------------------
/** For a peer that turned out to be banned by IPv4, this function increases
 *   the trigger count.
 *  \param ip_start Start of IP ban range corresponding to peer.
 *  \param ip_end End of IP ban range corresponding to peer.
 */
void DatabaseConnector::increaseIpBanTriggerCount(uint32_t ip_start, uint32_t ip_end) const
{
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET trigger_count = trigger_count + 1, "
        "last_trigger = datetime('now') "
        "WHERE ip_start = %u AND ip_end = %u;",
        ServerConfig::m_ip_ban_table.c_str(), ip_start, ip_end);
    easySQLQuery(query);
}   // getIpBanTableData

//-----------------------------------------------------------------------------
/** Gets the rows from IPv6 ban table, either all of them (for polling
 *   purposes), or those describing a certain address (if only one peer has to
 *   be checked).
 *  \param ip The IPv6 address to check the database for. If empty, all rows
 *            will be given.
 *  \return A vector of rows in the form of Ipv6BanTableData structures.
 */
std::vector<DatabaseConnector::Ipv6BanTableData>
DatabaseConnector::getIpv6BanTableData(std::string ipv6) const
{
    std::vector<Ipv6BanTableData> result;
    if (!m_ipv6_ban_table_exists)
    {
        return result;
    }
    bool single_ip = !ipv6.empty();
    std::string query;
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();

    query = StringUtils::insertValues(
        "SELECT rowid, ipv6_cidr, reason, description FROM %s WHERE ",
        ServerConfig::m_ipv6_ban_table.c_str()
    );
    if (single_ip)
        query += StringUtils::insertValues(
            "insideIPv6CIDR(ipv6_cidr, %s) = 1 AND ",
            Binder(coll, ipv6, "ipv6")
        );

    query += "datetime('now') > datetime(starting_time) AND "
        "(expired_days is NULL OR datetime"
        "(starting_time, '+'||expired_days||' days') > datetime('now'))";

    if (single_ip)
        query += " LIMIT 1;";

    std::vector<std::vector<std::string>> output;
    easySQLQuery(query, &output, coll->getBindFunction());

    for (std::vector<std::string>& row: output)
    {
        Ipv6BanTableData element;
        if (!StringUtils::fromString(row[0], element.row_id))
            continue;
        element.ipv6_cidr = row[1];
        element.reason = row[2];
        element.description = row[3];
        result.push_back(element);
    }
    return result;
}   // getIpv6BanTableData

//-----------------------------------------------------------------------------
/** For a peer that turned out to be banned by IPv6, this function increases
 *   the trigger count.
 *  \param ipv6_cidr Block of IPv6 addresses corresponding to the peer.
 */
void DatabaseConnector::increaseIpv6BanTriggerCount(const std::string& ipv6_cidr) const
{
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET trigger_count = trigger_count + 1, "
        "last_trigger = datetime('now') "
        "WHERE ipv6_cidr = %s;",
        ServerConfig::m_ipv6_ban_table.c_str(),
        Binder(coll, ipv6_cidr, "ipv6_cidr")
    );
    easySQLQuery(query, nullptr, coll->getBindFunction());
}   // increaseIpv6BanTriggerCount

//-----------------------------------------------------------------------------
/** Gets the rows from online id ban table, either all of them (for polling
 *   purposes), or those describing a certain online id (if only one peer has
 *   to be checked).
 *  \param online_id The online id to check the database for. If empty, all
 *                   rows will be given.
 *  \return A vector of rows in the form of OnlineIdBanTableData structures.
 */
std::vector<DatabaseConnector::OnlineIdBanTableData>
DatabaseConnector::getOnlineIdBanTableData(uint32_t online_id) const
{
    std::vector<OnlineIdBanTableData> result;
    if (!m_online_id_ban_table_exists)
    {
        return result;
    }
    bool single_id = (online_id != 0);
    std::ostringstream oss;
    oss << "SELECT rowid, online_id, reason, description FROM ";
    oss << (std::string)ServerConfig::m_online_id_ban_table;
    oss << " WHERE ";
    if (single_id)
        oss << "online_id = " << online_id << " AND ";
    oss << "datetime('now') > datetime(starting_time) AND "
        "(expired_days is NULL OR datetime"
        "(starting_time, '+'||expired_days||' days') > datetime('now'))";
    if (single_id)
        oss << " LIMIT 1";
    oss << ";";
    std::string query = oss.str();
    sqlite3_exec(m_db, query.c_str(),
        [](void* ptr, int count, char** data, char** columns)
        {
            std::vector<OnlineIdBanTableData>* vec = (std::vector<OnlineIdBanTableData>*)ptr;
            OnlineIdBanTableData element;
            if (!StringUtils::fromString(data[0], element.row_id))
                return 0;
            if (!StringUtils::fromString(data[1], element.online_id))
                return 0;
            element.reason = std::string(data[2]);
            element.description = std::string(data[3]);
            vec->push_back(element);
            return 0;
        }, &result, NULL);
    return result;
}   // getOnlineIdBanTableData

//-----------------------------------------------------------------------------
/** For a peer that turned out to be banned by online id, this function
 *   increases the trigger count.
 *  \param online_id Online id of the peer.
 */
void DatabaseConnector::increaseOnlineIdBanTriggerCount(uint32_t online_id) const
{
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET trigger_count = trigger_count + 1, "
        "last_trigger = datetime('now') "
        "WHERE online_id = %u;",
        ServerConfig::m_online_id_ban_table.c_str(), online_id);
    easySQLQuery(query);
}   // increaseOnlineIdBanTriggerCount

//-----------------------------------------------------------------------------
/** Clears reports that are older than a certain number of days
 *   (specified in the server config).
 */
void DatabaseConnector::clearOldReports()
{
    if (m_player_reports_table_exists &&
        ServerConfig::m_player_reports_expired_days != 0.0f)
    {
        std::string query = StringUtils::insertValues(
            "DELETE FROM %s "
            "WHERE datetime"
            "(reported_time, '+%f days') < datetime('now');",
            ServerConfig::m_player_reports_table.c_str(),
            ServerConfig::m_player_reports_expired_days);
        easySQLQuery(query);
    }
}   // clearOldReports

//-----------------------------------------------------------------------------
/** Sets disconnection times for those peers that already left the server, but
 *   whose disconnection times wasn't set yet.
 *  \param present_hosts List of online ids of present peers.
 */
void DatabaseConnector::setDisconnectionTimes(std::vector<uint32_t>& present_hosts)
{
    if (!hasServerStatsTable())
        return;
    std::ostringstream oss;
        oss << "UPDATE " << m_server_stats_table
            << "    SET disconnected_time = datetime('now')"
            << "    WHERE connected_time = disconnected_time";
    if (present_hosts.empty())
    {
        oss << ";";
    }
    else
    {
        oss << " AND host_id NOT IN (";
        for (unsigned i = 0; i < present_hosts.size(); i++)
        {
            if (i > 0)
                oss << ",";
            oss << present_hosts[i];
        }
        oss << ");";
    }
    std::string query = oss.str();
    easySQLQuery(query);
}   // setDisconnectionTimes

//-----------------------------------------------------------------------------
/** Adds a specified IP address to the IPv4 ban table. Usually invoked from
 *   network console.
 *  \param addr Address to ban.
 */
void DatabaseConnector::saveAddressToIpBanTable(const SocketAddress& addr)
{
    if (addr.isIPv6() || !m_db || !m_ip_ban_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (ip_start, ip_end) "
        "VALUES (%u, %u);",
        ServerConfig::m_ip_ban_table.c_str(), addr.getIP(), addr.getIP());
    easySQLQuery(query);
}   // saveAddressToIpBanTable

//-----------------------------------------------------------------------------
/** Called when the player joins the server, inserts player info into database.
 *  \param peer The peer that joins.
 *  \param online_id Player's online id.
 *  \param player_count Number of players joining using a single peer.
 *  \param country_code Country code deduced by global or local IP mapping.
 */
void DatabaseConnector::onPlayerJoinQueries(std::shared_ptr<STKPeer> peer,
        uint32_t online_id, unsigned player_count, const std::string& country_code)
{
    if (m_server_stats_table.empty() || peer->isAIPeer())
        return;
    std::string query;
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    auto version_os = StringUtils::extractVersionOS(peer->getUserVersion());
    if (ServerConfig::m_ipv6_connection && peer->getAddress().isIPv6())
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(host_id, ip, ipv6, port, online_id, username, player_num, "
            "country_code, version, os, ping"
            ", addon_karts_count, addon_tracks_count"
            ", addon_arenas_count, addon_soccers_count) "
            "VALUES (%u, 0, \"%s\", %u, %u, %s, %u, %s, %s, %s, %u"
            ", %d, %d, %d, %d);",
            m_server_stats_table.c_str(),
            peer->getHostId(),
            peer->getAddress().toString(false),
            peer->getAddress().getPort(),
            online_id,
            Binder(coll, StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()), "player_name"),
            player_count,
            Binder(coll, country_code, "country_code", true),
            Binder(coll, version_os.first, "version"),
            Binder(coll, version_os.second, "os"),
            peer->getAveragePing(),
            peer->addon_karts_count,
            peer->addon_tracks_count,
            peer->addon_arenas_count,
            peer->addon_soccers_count
        );
    }
    else
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(host_id, ip, port, online_id, username, player_num, "
            "country_code, version, os, ping"
            ", addon_karts_count, addon_tracks_count"
            ", addon_arenas_count, addon_soccers_count) "
            "VALUES (%u, %u, %u, %u, %s, %u, %s, %s, %s, %u"
            ", %d, %d, %d, %d);",
            m_server_stats_table.c_str(),
            peer->getHostId(),
            peer->getAddress().getIP(),
            peer->getAddress().getPort(),
            online_id,
            Binder(coll, StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()), "player_name"),
            player_count,
            Binder(coll, country_code, "country_code", true),
            Binder(coll, version_os.first, "version"),
            Binder(coll, version_os.second, "os"),
            peer->getAveragePing(),
            peer->addon_karts_count,
            peer->addon_tracks_count,
            peer->addon_arenas_count,
            peer->addon_soccers_count
        );
    }
    easySQLQuery(query, nullptr, coll->getBindFunction());
}   // onPlayerJoinQueries

//-----------------------------------------------------------------------------
/** Prints all rows of the IPv4 ban table. Called from the network console. */
void DatabaseConnector::listBanTable()
{
    if (!m_db)
        return;
    auto printer = [](void* data, int argc, char** argv, char** name)
    {
        for (int i = 0; i < argc; i++)
        {
            std::cout << name[i] << " = " << (argv[i] ? argv[i] : "NULL")
                << "\n";
        }
        std::cout << "\n";
        return 0;
    };
    if (m_ip_ban_table_exists)
    {
        std::string query = "SELECT * FROM ";
        query += ServerConfig::m_ip_ban_table;
        query += ";";
        std::cout << "IP ban list:\n";
        sqlite3_exec(m_db, query.c_str(), printer, NULL, NULL);
    }
    if (m_online_id_ban_table_exists)
    {
        std::string query = "SELECT * FROM ";
        query += ServerConfig::m_online_id_ban_table;
        query += ";";
        std::cout << "Online Id ban list:\n";
        sqlite3_exec(m_db, query.c_str(), printer, NULL, NULL);
    }
}   // listBanTable

//-----------------------------------------------------------------------------
/** Gets a list of messages (currently at most one) sent to an online account
 *   from the server owner.
 *  \param online_id Corresponds to user for whom the messages are retrieved.
 *  \return A vector of rows in the form of IpBanTableData structures.
 */
std::vector<DatabaseConnector::ServerMessage>
DatabaseConnector::getServerMessages(uint32_t online_id) const
{
    std::vector<ServerMessage> result;

    if (!hasDatabase() || !hasPlayerReportsTable() || online_id == 0)
        return result;

    // At most one message for now, sorry
    std::vector<std::vector<std::string>> output;
    std::string query = StringUtils::insertValues(
        "SELECT rowid, reported_time, info FROM \"%s\" "
        "WHERE reporter_online_id = %u "
        "AND reporting_online_id = -1 "
        "AND (server_uid = \"\" OR server_uid = \"%s\") LIMIT 1;",
        ServerConfig::m_player_reports_table.c_str(),
        online_id, ServerConfig::m_server_uid.c_str());

    if (!easySQLQuery(query, &output))
        return result;
    for (std::vector<std::string>& row: output)
    {
        ServerMessage element;
        if (!StringUtils::fromString(row[0], element.row_id))
            continue;
        element.timestamp = row[1];
        element.message = row[2];
        result.push_back(element);
    }
    return result;
}   // getServerMessages

//-----------------------------------------------------------------------------
/** Deletes a specified server-to-player message.
 *  \param row_id Row id of the message.
 */
void DatabaseConnector::deleteServerMessage(int row_id) const
{
    std::string query = StringUtils::insertValues(
            "DELETE FROM \"%s\" WHERE rowid = %u;",
            ServerConfig::m_player_reports_table.c_str(), row_id);
    easySQLQuery(query);
}   // deleteServerMessage

//-----------------------------------------------------------------------------
/** Queries the database for the best ever result set under certain settings.
 *  \param game_info (input) Settings of the game used.
 *  \param exists (output) Whether the results exist with that config.
 *  \param user (output) Name of user that set the best result.
 *  \param result (output) The result itself.
 *  \return True if the query succeeded (even if the set of results is empty).
 */
bool DatabaseConnector::getBestResult(const GameInfo& game_info,
                            bool* exists, std::string* user, double* result)
{
    if (!m_records_table_exists)
        return false;
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    // Note that IS is important, as the strings corresponding to value/time
    // limits can be NULL instead. SQLite manual specifies that IS can be used
    // to compare strings just as = operator.
    std::string query = StringUtils::insertValues("SELECT username, "
        "result FROM \"%s\" WHERE venue = %s AND reverse = \"%s\" "
        "AND mode = \"%s\" AND value_limit = %s AND time_limit = %s "
        "AND config = %s AND items = %s AND is_not_full = 0 AND game_event = 0 "
        "ORDER BY result ASC, time ASC LIMIT 1;",
        ServerConfig::m_records_table_name.c_str(),
        Binder(coll, game_info.m_venue, "map name"),
        game_info.m_reverse.c_str(),
        game_info.m_mode.c_str(),
        game_info.m_value_limit,
        game_info.m_time_limit,
        Binder(coll, game_info.m_kart_char_string, "kart char string"),
        Binder(coll, game_info.m_powerup_string, "powerup string")
    );
    std::vector<std::vector<std::string>> output;
    if (easySQLQuery(query, &output, coll->getBindFunction()))
    {
        if (output.size() >= 1)
        {
            double value;
            if (StringUtils::fromString(output[0][1], value))
            {
                *exists = true;
                *user = output[0][0];
                *result = value;
                return true;
            }
        }
        else
        {
            *exists = false;
            *user = "";
            *result = 0.0;
            return true;
        }
    }
    return false;
}   // getBestResult

//-----------------------------------------------------------------------------
/** Inserts all the results of a single game. The parameters will be reordered
 *   later.
 *  \param game_info (input) Settings of the game used. Includes the results
 *                   themselves.
 */
void DatabaseConnector::insertManyResults(const GameInfo& game_info)
{
    for (int i = 0; i < (int)game_info.m_player_info.size(); ++i)
    {
        const GameInfo::PlayerInfo& pi = game_info.m_player_info[i];
        std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
        // Note that insertValues doesn't care about the exact letter, so we
        // can use %f for StringUtils::Precision, for example
        std::string query = StringUtils::insertValues(
            "INSERT INTO %s (time, venue, reverse, mode, value_limit, time_limit, "
            "difficulty, config, items, flag_return_timeout, flag_deactivated_time, "
            "username, result, kart, kart_class, kart_color, team, handicap, start_pos, fastest_lap, sog_time, "
            "online_id, country_code, is_autofinish, is_not_full, game_duration, when_joined, when_left, "
            "game_event, other_info) "
            "VALUES (%s, %s, \"%s\", \"%s\", %d, %f, "
            "%d, %s, %s, %d, %d, " //%s, "
            "%s, %f, %s, %s, %f, %d, %d, %d, %f, %f, "
            "%d, \"%s\", %d, %d, %f, %f, %f, "
            "%d, %s);",
            m_results_table_name.c_str(),
            Binder(coll, game_info.m_timestamp, "time"),
            Binder(coll, game_info.m_venue, "map name"),
            game_info.m_reverse.c_str(),
            game_info.m_mode.c_str(),
            game_info.m_value_limit,
            StringUtils::Precision(game_info.m_time_limit, 6),
            game_info.m_difficulty,
            Binder(coll, game_info.m_kart_char_string, "kart char string"),
            Binder(coll, game_info.m_powerup_string, "powerup string"),
            game_info.m_flag_return_timeout,
            game_info.m_flag_deactivated_time,
            Binder(coll, pi.m_username, "username"),
            StringUtils::Precision(pi.m_result, 6),
            Binder(coll, pi.m_kart.c_str(), "kart name"),
            Binder(coll, pi.m_kart_class, "kart class"),
            StringUtils::Precision(pi.m_kart_color, 2),
            pi.m_team,
            pi.m_handicap,
            pi.m_start_position,
            StringUtils::Precision(pi.m_fastest_lap, 6),
            StringUtils::Precision(pi.m_sog_time, 6),
            pi.m_online_id,
            pi.m_country_code.c_str(),
            pi.m_autofinish,
            pi.m_not_full,
            StringUtils::Precision(pi.m_game_duration, 6),
            StringUtils::Precision(pi.m_when_joined, 6),
            StringUtils::Precision(pi.m_when_left, 6),
            pi.m_game_event,
            Binder(coll, pi.m_other_info, "other info")
        );
        easySQLQuery(query, nullptr, coll->getBindFunction());
    }
}   // insertManyResults

//-----------------------------------------------------------------------------

#endif // ENABLE_SQLITE3



