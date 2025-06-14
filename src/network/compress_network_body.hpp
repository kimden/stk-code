//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
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

#ifndef HEADER_COMPRESS_NETWORK_BODY_HPP
#define HEADER_COMPRESS_NETWORK_BODY_HPP

#include "network/network_string.hpp"
#include "mini_glm.hpp"

#include "LinearMath/btMotionState.h"
#include "btBulletDynamicsCommon.h"

namespace CompressNetworkBody
{
    using namespace MiniGLM;
    // ------------------------------------------------------------------------
    /** Set body and motion state of bullet object with compressed values. */
    inline void setCompressedValues(float x, float y, float z,
                                    uint32_t compressed_q,
                                    short lvx, short lvy, short lvz,
                                    short avx, short avy, short avz,
                                    btRigidBody* body, btMotionState* ms)
    {
        btTransform trans;
        trans.setOrigin(btVector3(x,y,z));
        trans.setRotation(decompressbtQuaternion(compressed_q));
        btVector3 lv(toFloat32(lvx), toFloat32(lvy), toFloat32(lvz));
        btVector3 av(toFloat32(avx), toFloat32(avy), toFloat32(avz));

        body->setWorldTransform(trans);
        ms->setWorldTransform(trans);
        body->setInterpolationWorldTransform(trans);
        body->setLinearVelocity(lv);
        body->setAngularVelocity(av);
        body->setInterpolationLinearVelocity(lv);
        body->setInterpolationAngularVelocity(av);
        body->updateInertiaTensor();
    }   // setCompressedValues
    // ------------------------------------------------------------------------
    /** Compress transformation and velocities of bullet object, it will
     *  call MiniGLM::compressQuaternion for compress quaternion of
     *  transformation and convert linear and angular velocities to half floats
     *  it can be used by client to locally round values to make sure client
     *  and server have similar state when saving state if you don't provoide
     *  bns.
     */
    inline CompressedNetworkBodyPacket compress(btRigidBody* body, btMotionState* ms,
                         BareNetworkString* bns = NULL)
    {
        float x = body->getWorldTransform().getOrigin().x();
        float y = body->getWorldTransform().getOrigin().y();
        float z = body->getWorldTransform().getOrigin().z();
        uint32_t compressed_q =
            compressQuaternion(body->getWorldTransform().getRotation());
        short lvx = toFloat16(body->getLinearVelocity().x());
        short lvy = toFloat16(body->getLinearVelocity().y());
        short lvz = toFloat16(body->getLinearVelocity().z());
        short avx = toFloat16(body->getAngularVelocity().x());
        short avy = toFloat16(body->getAngularVelocity().y());
        short avz = toFloat16(body->getAngularVelocity().z());
        setCompressedValues(x, y, z, compressed_q, lvx, lvy, lvz, avx, avy,
            avz, body, ms);

        // if bns is null, it's locally compressed (for rounding values)
        // in that case, we simply disregard the result, and before it returned NULL

        CompressedNetworkBodyPacket packet;
        packet.x = x;
        packet.y = y;
        packet.z = z;
        packet.compressed_q = compressed_q;
        packet.lvx = lvx;
        packet.lvy = lvy;
        packet.lvz = lvz;
        packet.avx = avx;
        packet.avy = avy;
        packet.avz = avz;

        return packet;
    }   // compress
    // ------------------------------------------------------------------------
    /* Called during rewind when restoring data from game state. */
    inline void decompress(const CompressedNetworkBodyPacket& packet,
                           btRigidBody* body, btMotionState* ms)
    {
        float x = packet.x;
        float y = packet.y;
        float z = packet.z;
        uint32_t compressed_q = packet.compressed_q;
        short lvx = packet.lvx;
        short lvy = packet.lvy;
        short lvz = packet.lvz;
        short avx = packet.avx;
        short avy = packet.avy;
        short avz = packet.avz;
        setCompressedValues(x, y, z, compressed_q, lvx, lvy, lvz, avx, avy,
            avz, body, ms);
    }   // decompress
};

#endif // HEADER_COMPRESS_NETWORK_BODY_HPP
