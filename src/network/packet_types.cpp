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

#include "network/packet_types.hpp"
#include "network/remote_kart_info.hpp"
#include "network/network_string.hpp"

namespace
{
    template<typename T>
    void setParentMaybePacket(T& value, Packet* pointer, std::true_type)
    {
        Packet* current = &value;
        current->parent = pointer;
        for (auto& p: pointer->m_storage)
        {
            value.m_storage[p.first] = p.second;
        }
    }
    
    template<typename T>
    void setParentMaybePacket(T& value, Packet* pointer, std::false_type)
    {
        // Not a packet, do nothing.
    }
    
    template<typename T>
    void setParent(T& value, Packet* pointer)
    {
        setParentMaybePacket(value, pointer, std::is_base_of<Packet, T>());
    }
}

//---------------------- To NetworkString -------------------------------------

#define DEFINE_CLASS(Name) \
void Name::toNetworkString(NetworkString* ns) const \
{

#define DEFINE_DERIVED_CLASS(Name, Parent) DEFINE_CLASS(Name)

#define PROTOCOL_TYPE(Type, Sync) \
    ns->setProtocolType(Type); \
    if (!m_override_synchronous.has_value()) \
        ns->setSynchronous(Sync); \
    else \
        ns->setSynchronous(m_override_synchronous.get_value());

#define AUX_STORE(Key, Var)

#define AUX_VAR(Type, Var)

#define AUX_VAR_VALUE(Type, Var, Value)

#define AUX_VAR_FROM_PARENT(Type, Key, Var)

#define DEFINE_FIELD(Type, Var) \
    ns->encode<Type>(Var);

#define DEFINE_FIELD16(Type, Var) \
    ns->encodeString16(Var);

#define DEFINE_FIELD_PTR(Type, Var) \
    if (Var) \
        ns->encode<Type>(*Var);

// We send it if it exists, and receive only if the condition is true
#define DEFINE_FIELD_OPTIONAL(Type, Var, Condition) \
    if (Var.has_value()) \
        ns->encode<Type>(Var.get_value());

#define DEFINE_TYPE(Type, Var, Value) \
    ns->encode<Type>(Value);

#define DEFINE_VECTOR(Type, Size, Value) \
    for (unsigned Value##_cnt = 0; Value##_cnt < Value.size(); ++Value##_cnt) { \
        ns->encode<Type>(Value[Value##_cnt]); \
    }

/* Might have additional size inconsistencies due to nullptrs being omitted. */
#define DEFINE_VECTOR_PTR(Type, Size, Value) \
    for (unsigned Value##_cnt = 0; Value##_cnt < Value.size(); ++Value##_cnt) { \
        if (Value[Value##_cnt]) \
            ns->encode<Type>(*(Value[Value##_cnt])); \
    }

#define RELIABLE(Value)

#define END_DEFINE_CLASS(Name) \
}

#include "network/packet_types_base.hpp"
#undef DEFINE_CLASS
#undef DEFINE_DERIVED_CLASS
#undef PROTOCOL_TYPE
#undef AUX_STORE
#undef AUX_VAR
#undef AUX_VAR_VALUE
#undef AUX_VAR_FROM_PARENT
#undef DEFINE_FIELD
#undef DEFINE_FIELD16
#undef DEFINE_FIELD_PTR
#undef DEFINE_TYPE
#undef DEFINE_FIELD_OPTIONAL
#undef DEFINE_VECTOR
#undef DEFINE_VECTOR_PTR
#undef RELIABLE
#undef END_DEFINE_CLASS




//---------------------- From NetworkString -----------------------------------

#define DEFINE_CLASS(Name) \
void Name::fromNetworkString(NetworkString* ns) \
{ 

#define DEFINE_DERIVED_CLASS(Name, Parent) DEFINE_CLASS(Name)

#define PROTOCOL_TYPE(Type, Sync)

#define AUX_STORE(Key, Var) \
    store(Key, Var);

#define AUX_VAR(Type, Var)

#define AUX_VAR_VALUE(Type, Var, Value) \
    Var = (Value);

#define AUX_VAR_FROM_PARENT(Type, Key, Var) \
    Var = parent->obtain<Type>(Key);

#define DEFINE_FIELD(Type, Var) \
    setParent(Var, this); \
    ns->decode<Type>(Var);

#define DEFINE_FIELD16(Type, Var) \
    ns->decodeString16(&Var);

// Same as optional but unconditional
#define DEFINE_FIELD_PTR(Type, Var) \
    Type temp_##Var; \
    setParent(temp_##Var, this); \
    ns->decode<Type>(temp_##Var); \
    Var = std::make_shared<Type>(temp_##Var); \

// We send it if it exists, and receive only if the condition is true
#define DEFINE_FIELD_OPTIONAL(Type, Var, Condition) \
    if (Condition) { \
        int temp_prev_offset = ns->getCurrentOffset(); \
        Type temp_##Var; \
        setParent(temp_##Var, this); \
        try { \
            ns->decode<Type>(temp_##Var); \
        } catch (...) { \
            ns->setCurrentOffset(temp_prev_offset); \
        } \
        Var = temp_##Var; \
    }

#define DEFINE_TYPE(Type, Var, Value)

#define DEFINE_VECTOR(Type, Size, Var) \
    Var.resize(Size); \
    for (unsigned Var##_cnt = 0; Var##_cnt < Size; ++Var##_cnt) { \
        setParent(Var[Var##_cnt], this); \
        ns->decode<Type>(Var[Var##_cnt]); \
    }

#define DEFINE_VECTOR_PTR(Type, Size, Var) \
    Var.resize(Size); \
    for (unsigned Var##_cnt = 0; Var##_cnt < Size; ++Var##_cnt) { \
        Type temp_##Var; \
        setParent(temp_##Var, this); \
        ns->decode<Type>(temp_##Var); \
        Var[Var##_cnt] = std::make_shared<Type>(temp_##Var); \
    }

#define RELIABLE(Value)

#define END_DEFINE_CLASS(Name) \
}

#include "network/packet_types_base.hpp"
#undef DEFINE_CLASS
#undef DEFINE_DERIVED_CLASS
#undef PROTOCOL_TYPE
#undef AUX_STORE
#undef AUX_VAR
#undef AUX_VAR_VALUE
#undef AUX_VAR_FROM_PARENT
#undef DEFINE_FIELD
#undef DEFINE_FIELD16
#undef DEFINE_FIELD_PTR
#undef DEFINE_TYPE
#undef DEFINE_FIELD_OPTIONAL
#undef DEFINE_VECTOR
#undef DEFINE_VECTOR_PTR
#undef RELIABLE
#undef END_DEFINE_CLASS

//---------------------- End --------------------------------------------------