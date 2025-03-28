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
KartData::KartData(const KartDataPacket& packet)
{
    m_kart_type = packet.kart_type;
    if (!m_kart_type.empty())
    {
        m_width = packet.parameters->width;
        m_height = packet.parameters->height;
        m_length = packet.parameters->length;
        m_gravity_shift = packet.parameters->gravity_shift;
    }
    else
    {
        m_width = 0.0f;
        m_height = 0.0f;
        m_length = 0.0f;
    }
}   // KartData(KartDataPacket&)

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
