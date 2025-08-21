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

#include "utils/command_manager/auth_resource.hpp"
#include "network/crypto.hpp"
#include "utils/string_utils.hpp"
#include "utils/time.hpp"

void AuthResource::fromXmlNode(const XMLNode* node)
{                
    node->get("secret", &m_secret);
    node->get("server", &m_server);
    node->get("link-format", &m_link_format);
} // AuthResource::AuthResource
//-----------------------------------------------------------------------------

std::string AuthResource::get(const std::string& username, int online_id)
{
#ifdef ENABLE_CRYPTO_OPENSSL
    std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    uint64_t timestamp = StkTime::getTimeSinceEpoch();
    std::string payload = "{\"sub\":\"" + username + "/" + std::to_string(online_id) + "\",";
    payload += "\"iat\":\"" + std::to_string(timestamp) + "\",";
    payload += "\"iss\":\"" + m_server + "\"}";
    header = Crypto::base64url(StringUtils::toUInt8Vector(header));
    payload = Crypto::base64url(StringUtils::toUInt8Vector(payload));
    std::string message = header + "." + payload;
    std::string signature = Crypto::base64url(Crypto::hmac_sha256_array(m_secret, message));
    std::string token = message + "." + signature;
    std::string response = StringUtils::insertValues(m_link_format, token.c_str());
    return response;
#else
    return "This command is currently only supported for OpenSSL";
#endif
}   // AuthResource::get
//-----------------------------------------------------------------------------