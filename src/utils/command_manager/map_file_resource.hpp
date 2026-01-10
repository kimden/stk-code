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


#ifndef MAP_FILE_RESOURCE_HPP
#define MAP_FILE_RESOURCE_HPP

#include "utils/types.hpp"
#include "utils/set_typo_fixer.hpp"
#include "utils/command_manager/file_resource.hpp"
#include <string>

struct MapFileResource: public FileResource
{
private:
    std::string m_print_format;
    char m_line_delimiter = '\n'; // Line breaks should be checked on Windows
    char m_field_delimiter = ' ';
    char m_out_line_delimiter;
    char m_out_field_delimiter;
    char m_left_quote = '"';
    char m_right_quote = '"';
    char m_escape = '\\';

    // Technically nothing prevents us from making several indices.
    // Except the lack of practical need, of course.
    int m_index = 0;

    int m_public = -1;

    struct Row
    {
        std::vector<std::string> cells;
    };

    std::vector<std::shared_ptr<Row>> m_rows;
    std::map<std::string, std::vector<std::shared_ptr<Row>>> m_indexed;

    SetTypoFixer m_fixer;

protected:
    void onContentChange() override;

public:
    virtual void fromXmlNode(const XMLNode* node) override;

    virtual void execute(Context& context) override;
};


#endif // MAP_FILE_RESOURCE_HPP
