#include "network/kart_data.hpp"

#include "karts/kart_model.hpp"
#include "karts/kart_properties.hpp"
#include "network/packet_types.hpp"

// ----------------------------------------------------------------------------
KartData::KartData(const KartProperties* kp)
{
    m_kart_type = kp->getKartType();
    if (!m_kart_type.empty())
    {
        m_width  = kp->getMasterKartModel().getWidth();
        m_height = kp->getMasterKartModel().getHeight();
        m_length = kp->getMasterKartModel().getLength();
        m_gravity_shift = kp->getGravityCenterShift();
    }
    else
    {
        m_width = 0.0f;
        m_height = 0.0f;
        m_length = 0.0f;
    }
}   // KartData(KartProperties*)

// ----------------------------------------------------------------------------
KartData::KartData(const BareNetworkString& ns)
{
    ns.decodeString(&m_kart_type);
    if (!m_kart_type.empty())
    {
        m_width = ns.getFloat();
        m_height = ns.getFloat();
        m_length = ns.getFloat();
        m_gravity_shift = ns.getVec3();
    }
    else
    {
        m_width = 0.0f;
        m_height = 0.0f;
        m_length = 0.0f;
    }
}   // KartData(BareNetworkString&)

// ----------------------------------------------------------------------------
KartDataPacket KartData::encode() const
{
    KartDataPacket packet;
    packet.kart_type = m_kart_type;
    packet.parameters = {};

    if (!m_kart_type.empty())
    {
        KartParametersPacket params;
        params.width = m_width;
        params.height = m_height;
        params.length = m_length;
        params.gravity_shift = m_gravity_shift;
        packet.parameters = std::make_shared<KartParametersPacket>(params);
    }

    return packet;
}   // encode(BareNetworkString*)
