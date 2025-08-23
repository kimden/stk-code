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

#include "utils/command_manager/map_file_resource.hpp"

#include "utils/string_utils.hpp"

namespace
{
    int readChar(const XMLNode* node, std::string&& name, char* value)
    {
        std::string temp(1, *value);
        auto res = node->get(std::move(name), &temp);
        temp = StringUtils::wideToUtf8(StringUtils::xmlDecode(temp));
        if (res == 0 || temp.empty())
            return 0;
        
        *value = temp[0];
        return 1;
    }
}   // namespace
//-----------------------------------------------------------------------------

void MapFileResource::fromXmlNode(const XMLNode* node)
{
    FileResource::fromXmlNode(node);
    node->get("print-format", &m_print_format);
    readChar(node, "line-delimiter", &m_line_delimiter);
    readChar(node, "field-delimiter", &m_field_delimiter);

    if (!readChar(node, "out-line-delimiter", &m_out_line_delimiter))
        m_out_line_delimiter = m_line_delimiter;
    if (!readChar(node, "out-field-delimiter", &m_out_field_delimiter))
        m_out_field_delimiter = m_field_delimiter;

    readChar(node, "out-field-delimiter", &m_out_field_delimiter);
    readChar(node, "left-quote", &m_left_quote);
    readChar(node, "right-quote", &m_right_quote);
    readChar(node, "escape", &m_escape);
    node->get("index", &m_index);
}   // MapFileResource
//-----------------------------------------------------------------------------

void MapFileResource::onContentChange()
{
    auto rows = StringUtils::splitQuoted(m_contents, m_line_delimiter, m_left_quote, m_right_quote, m_escape);
    m_rows.resize(rows.size());
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        m_rows[i] = std::make_shared<Row>();
        m_rows[i]->cells = StringUtils::splitQuoted(rows[i], m_field_delimiter, m_left_quote, m_right_quote, m_escape);
    }

    m_fixer.clear();
    m_indexed.clear();
    if (m_index >= 0)
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            auto& container = m_rows[i];
            if (m_index < (int)container->cells.size())
            {
                const std::string key = container->cells[m_index];
                m_fixer.add(key);
                m_indexed[key].push_back(container);
            }
        }
    }
}   // onContentChange
//-----------------------------------------------------------------------------

void MapFileResource::execute(Context& context)
{
    tryUpdate();

    auto& argv = context.m_argv;
    auto acting_peer = context.actingPeer();
    auto peer = context.peer();

    if (argv.size() < 2)
    {
        context.error();
        return;
    }

    // I'm not sure if we should search by username.
    // After all, the search column might contain something else.
    if (argv[1] == "search")
    {
        if (2 >= argv.size())
        {
            context.error();
            return;
        }

        std::string key = argv[2];
        // Add fixing typos later
        auto it = m_indexed.find(key);
        if (it == m_indexed.end())
            context.say(StringUtils::insertValues("Nothing found for %s", key.c_str()));
        else
        {
            std::string msg = "";
            int sz = it->second.size();
            if (sz >= 2)
                msg += StringUtils::insertValues("Found %s lines\n", sz);
            for (auto& row: it->second)
            {
                if (row)
                    msg += StringUtils::quoteEscapeArray(
                        row->cells.begin(), row->cells.end(), m_out_field_delimiter,
                        m_left_quote, m_right_quote, m_escape);

                msg += "\n";
            }
            msg.pop_back();
            context.say(msg);
        }
    }
    else
    {
        // Implement /command to and /command from to later
        context.error();
        return;
    }
}   // execute
//-----------------------------------------------------------------------------