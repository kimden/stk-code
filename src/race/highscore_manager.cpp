//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2006-2015 Joerg Henrichs
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

#include "race/highscore_manager.hpp"

#include <stdexcept>
#include <fstream>

#include "config/user_config.hpp"
#include "io/file_manager.hpp"
#include "io/utf_writer.hpp"
#include "race/race_manager.hpp"
#include "utils/constants.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"
#include "utils/translation.hpp"

HighscoreManager* highscore_manager=0;
const unsigned int HighscoreManager::CURRENT_HSCORE_FILE_VERSION = 4;

HighscoreManager::HighscoreManager()
{
    m_can_write=true;
    setFilename();
    loadHighscores();
}   // HighscoreManager

// -----------------------------------------------------------------------------
HighscoreManager::~HighscoreManager()
{
    saveHighscores();
    clearHighscores();

}   // ~HighscoreManager

// -----------------------------------------------------------------------------
/** Determines the path to store the highscore file in
 */
void HighscoreManager::setFilename()
{
    if ( getenv("SUPERTUXKART_HIGHSCOREDIR") != NULL )
    {
        m_filename = getenv("SUPERTUXKART_HIGHSCOREDIR")
                   + std::string("/highscore.xml");
    }
    else
    {
        m_filename=file_manager->getUserConfigFile("highscore.xml");
    }

    return;
}   // SetFilename

// -----------------------------------------------------------------------------
void HighscoreManager::loadHighscores()
{
    XMLNode *root = NULL;
    root = file_manager->createXMLTree(m_filename);
    if(!root)
    {
        saveHighscores();
        if(m_can_write)
        {
            Log::info("Highscore Manager", "New highscore file '%s' created.",
                    m_filename.c_str());
        }
        delete root;
        return;
    }

    try
    {
        if(!root || root->getName()!="highscores")
        {
            if(root) delete root;
            root = NULL;
            throw std::runtime_error("No 'highscore' node found.");
        }

        // check file version
        int v;
        if (!root->get("version", &v) || v<(int)CURRENT_HSCORE_FILE_VERSION)
        {
            Log::error("Highscore Manager", "Highscore file format too old, a new one will be created.");
            irr::core::stringw warning =
                _("The highscore file was too old,\nall highscores have been erased.");
            user_config->setWarning( warning );

            // since we haven't had the chance to load the current scores yet,
            // calling Save() now will generate an empty file with the right format.
            saveHighscores();
            delete root;
            root = NULL;
            return;
        }

        // read all entries one by one and store them in 'm_all_scores'
        for(unsigned int i=0; i<root->getNumNodes(); i++)
        {
            const XMLNode *node = root->getNode(i);
            Highscores *highscores;
            try
            {
                highscores = new Highscores(*node);
            }
            catch (std::logic_error& e)
            {
                Log::error("Highscore Manager", "Invalid highscore entry will be skipped : %s", e.what());
                continue;
            }
            m_all_scores.push_back(std::unique_ptr<Highscores>(highscores));
        }   // next entry

        if(UserConfigParams::logMisc())
            Log::error("Highscore Manager", "Highscores will be saved in '%s'.",
                    m_filename.c_str());
    }
    catch(std::exception& err)
    {
        Log::error("Highscore Manager", "Error while parsing highscore file '%s':",
                m_filename.c_str());
        Log::error("Highscore Manager", "%s", err.what());
        Log::error("Highscore Manager", "");
        Log::error("Highscore Manager", "No old highscores will be available.");
    }
    if(root)
        delete root;
}   // loadHighscores

// -----------------------------------------------------------------------------
void HighscoreManager::saveHighscores()
{
    // Print error message only once
    if(!m_can_write) return;

    try
    {
        // Save to a new file and rename later to avoid disk space problem, see #4709
        UTFWriter highscore_file((m_filename + "new").c_str(), false);
        highscore_file << "<?xml version=\"1.0\"?>\n";
        highscore_file << "<highscores version=\"" << CURRENT_HSCORE_FILE_VERSION << "\">\n";

        for(unsigned int i=0; i<m_all_scores.size(); i++)
        {
            m_all_scores[i]->writeEntry(highscore_file);
        }
        highscore_file << "</highscores>\n";
        highscore_file.close();
        file_manager->removeFile(m_filename);
        FileUtils::renameU8Path(m_filename + "new", m_filename);
    }
    catch(std::exception &e)
    {
        Log::error("Highscore Manager","Problems saving highscores in '%s' because %s",
            m_filename.c_str(), e.what());
        m_can_write=false;
    }

}   // saveHighscores

// -----------------------------------------------------------------------------
/*
 * Returns the high scores entry for a specific type of race.
 * Creates one if none exists yet.
 */
Highscores* HighscoreManager::getHighscores(const Highscores::HighscoreType &highscore_type,
                                            int num_karts,
                                            const RaceManager::Difficulty difficulty,
                                            const std::string &trackName,
                                            const int number_of_laps,
                                            const bool reverse)
{
    Highscores *highscores = 0;

    // See if we already have a record for this type
    for (auto& hs : m_all_scores)
    {
        if (hs->matches(highscore_type, num_karts, difficulty, trackName,
                         number_of_laps, reverse) )
        {
            // we found one entry for this kind of race, return it
            return hs.get();
        }
    }   // for i in m_all_scores

    // we don't have an entry for such a race currently. Create one.
    highscores = new Highscores(highscore_type, num_karts, difficulty,
                                trackName, number_of_laps, reverse);
    m_all_scores.push_back(std::unique_ptr<Highscores>(highscores));
    return highscores;
}   // getHighscores
// -----------------------------------------------------------------------------
Highscores* HighscoreManager::getGPHighscores(int num_karts,
                                              const RaceManager::Difficulty difficulty,
                                              const std::string &trackName,
                                              const int target,
                                              GrandPrixData::GPReverseType reverse_type,
                                              RaceManager::MinorRaceModeType minor_mode)
{
    Highscores *highscores = 0;

    // See if we already have a record for this type
    for (auto& hs : m_all_scores)
    {
        if (hs->matches(num_karts, difficulty, trackName, target, reverse_type, minor_mode))
        {
            // we found one entry for this kind of race, return it
            return hs.get();
        }
    }   // for i in m_all_scores

    // we don't have an entry for such a race currently. Create one.
    highscores = new Highscores(num_karts, difficulty,
                                trackName, target, reverse_type, minor_mode);
    m_all_scores.push_back(std::unique_ptr<Highscores>(highscores));
    return highscores;
} // getGPHighscores
