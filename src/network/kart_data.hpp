#ifndef KART_DATA_HPP
#define KART_DATA_HPP

#include "network/packet_types.hpp"
#include "utils/vec3.hpp"

#include <string>

class BareNetworkString;
class KartProperties;

class KartData
{
public:
    std::string m_kart_type;
    float m_width;
    float m_height;
    float m_length;
    Vec3 m_gravity_shift;
    // ------------------------------------------------------------------------
    KartData()
    {
        m_width = 0.0f;
        m_height = 0.0f;
        m_length = 0.0f;
    }
    // ------------------------------------------------------------------------
    KartData(const KartProperties* kp);
    // ------------------------------------------------------------------------
    KartData(const BareNetworkString& ns);
    // ------------------------------------------------------------------------
    KartDataPacket encode() const;

};   // class KartData

#endif // KART_DATA_HPP
