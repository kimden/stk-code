//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2025 kimden
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

#include "utils/command_manager/file_resource.hpp"

#include <fstream>
#include "utils/string_utils.hpp"
#include "utils/file_utils.hpp"
#include "utils/time.hpp"


void FileResource::fromXmlNode(const XMLNode* node)
{
    node->get("file", &m_file_name);
    node->get("interval", &m_interval);

    m_contents = "";
    m_last_invoked = 0;
    read();
}   // FileResource
//-----------------------------------------------------------------------------

void FileResource::read()
{
    if (m_file_name.empty()) // in case it is not properly initialized
        return;

    // idk what to do with absolute or relative paths
    const std::string& path = /*ServerConfig::getConfigDirectory() + "/" + */m_file_name;
    std::ifstream message(FileUtils::getPortableReadingPath(path));
    std::string answer;

    if (message.is_open())
    {
        for (std::string line; std::getline(message, line); )
        {
            answer += line;
            answer.push_back('\n');
        }
        if (!answer.empty())
            answer.pop_back();
    }

    m_contents = answer;
    m_last_invoked = StkTime::getMonoTimeMs();
}   // read
//-----------------------------------------------------------------------------

void FileResource::tryUpdate()
{
    uint64_t current_time = StkTime::getMonoTimeMs();
    if (m_interval != 0 && current_time >= m_interval + m_last_invoked)
        read();
}   // tryUpdate
//-----------------------------------------------------------------------------

std::string FileResource::get()
{
    tryUpdate();

    return m_contents;
}   // get
//-----------------------------------------------------------------------------