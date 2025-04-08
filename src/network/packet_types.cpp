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

//---------------------- To NetworkString -------------------------------------

#define DEFINE_CLASS(Name) \
inline void Name::toNetworkString(NetworkString* ns) const \
{

#define DEFINE_DERIVED_CLASS(Name, Parent) DEFINE_CLASS(Name)

#define SYNCHRONOUS(Value) \
    ns->setSynchronous(Value);

#define AUX_VAR(Type, Var)

#define DEFINE_FIELD(Type, Var) \
    ns->encode<Type>(Var);

#define DEFINE_FIELD_PACKET(Type, Var) \
    Var.toNetworkString(ns);

#define DEFINE_FIELD_PTR(Type, Var) \
    if (Var) \
        ns->encode<Type>(*Var);

// We send it if it exists, and receive only if the condition is true
#define DEFINE_FIELD_OPTIONAL(Type, Var, Condition) \
    if (Var) \
        ns->encode<Type>(*Var);

#define DEFINE_TYPE(Type, Var, Value) \
    ns->encode<Type>(Value);

#define DEFINE_VECTOR(Type, Size, Value) \
    for (unsigned Value##_cnt = 0; Value##_cnt < Size; ++Value##_cnt) { \
        ns->encode<Type>(Value[Value##_cnt]); \
    }

#define DEFINE_VECTOR_PACKET(Type, Size, Value) \
    for (unsigned Value##_cnt = 0; Value##_cnt < Size; ++Value##_cnt) { \
        Value[Value##_cnt].toNetworkString(ns); \
    }

#define DEFINE_VECTOR_PACKET_PTR(Type, Size, Value) \
    for (unsigned Value##_cnt = 0; Value##_cnt < Size; ++Value##_cnt) { \
        Value[Value##_cnt]->toNetworkString(ns); \
    }

#define RELIABLE()

#define END_DEFINE_CLASS(Name) \
}

#include "network/packet_types_base.hpp"
#undef DEFINE_CLASS
#undef DEFINE_DERIVED_CLASS
#undef SYNCHRONOUS
#undef AUX_VAR
#undef DEFINE_FIELD
#undef DEFINE_FIELD_PACKET
#undef DEFINE_FIELD_PTR
#undef DEFINE_TYPE
#undef DEFINE_FIELD_OPTIONAL
#undef DEFINE_VECTOR
#undef DEFINE_VECTOR_PACKET
#undef DEFINE_VECTOR_PACKET_PTR
#undef RELIABLE
#undef END_DEFINE_CLASS




//---------------------- From NetworkString -----------------------------------

#define DEFINE_CLASS(Name) \
inline void Name::fromNetworkString(NetworkString* ns) \
{ 

#define DEFINE_DERIVED_CLASS(Name, Parent) DEFINE_CLASS(Name)

#define SYNCHRONOUS(Value)

#define AUX_VAR(Type, Var)

#define DEFINE_FIELD(Type, Var) \
    ns->decode<Type>(Var);

#define DEFINE_FIELD_PACKET(Type, Var) \
    Var.fromNetworkString(ns);

// Same as optional but unconditional
#define DEFINE_FIELD_PTR(Type, Var) \
    Type temp_##Var; \
    ns->decode<Type>(temp_##Var); \
    Var = std::make_shared<Type>(temp_##Var); \

// We send it if it exists, and receive only if the condition is true
#define DEFINE_FIELD_OPTIONAL(Type, Var, Condition) \
    if (Condition) { \
        Type temp_##Var; \
        ns->decode<Type>(temp_##Var); \
        Var = std::make_shared<Type>(temp_##Var); \
    }

#define DEFINE_TYPE(Type, Var, Value)

#define DEFINE_VECTOR(Type, Size, Var) \
    Var.resize(Size); \
    for (unsigned Var##_cnt = 0; Var##_cnt < Size; ++Var##_cnt) { \
        ns->decode<Type>(Var[Var##_cnt]); \
    }

#define DEFINE_VECTOR_PACKET(Type, Size, Var) \
    Var.resize(Size); \
    for (unsigned Var##_cnt = 0; Var##_cnt < Size; ++Var##_cnt) { \
        Var[Var##_cnt].fromNetworkString(ns); \
    }

#define DEFINE_VECTOR_PACKET_PTR(Type, Size, Var) \
    Var.resize(Size); \
    for (unsigned Var##_cnt = 0; Var##_cnt < Size; ++Var##_cnt) { \
        Type temp_##Var; \
        temp_##Var.fromNetworkString(ns); \
        Var[Var##_cnt] = std::make_shared<Type>(temp_##Var); \
    }

#define RELIABLE()

#define END_DEFINE_CLASS(Name) \
}

#include "network/packet_types_base.hpp"
#undef DEFINE_CLASS
#undef DEFINE_DERIVED_CLASS
#undef SYNCHRONOUS
#undef AUX_VAR
#undef DEFINE_FIELD
#undef DEFINE_FIELD_PACKET
#undef DEFINE_FIELD_PTR
#undef DEFINE_TYPE
#undef DEFINE_FIELD_OPTIONAL
#undef DEFINE_VECTOR
#undef DEFINE_VECTOR_PACKET
#undef DEFINE_VECTOR_PACKET_PTR
#undef RELIABLE
#undef END_DEFINE_CLASS

//---------------------- End --------------------------------------------------