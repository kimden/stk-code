#ifndef GAME_EVENTS_PROTOCOL_HPP
#define GAME_EVENTS_PROTOCOL_HPP

#include "network/protocol.hpp"
#include "utils/cpp2011.hpp"

class AbstractKart;

class GameEventsProtocol : public Protocol
{
private:
    int m_last_finished_position;

    void eliminatePlayer(const NetworkString &ns);

public:
             GameEventsProtocol();
    virtual ~GameEventsProtocol();

    virtual bool notifyEvent(Event* event) OVERRIDE;
    void kartFinishedRace(AbstractKart *kart, float time);
    void kartFinishedRace(const NetworkString &ns);
    void sendStartupBoost(uint8_t kart_id);
    virtual void setup() OVERRIDE {}
    virtual void update(int ticks) OVERRIDE;
    virtual void asynchronousUpdate() OVERRIDE {}
    // ------------------------------------------------------------------------
    virtual bool notifyEventAsynchronous(Event* event) OVERRIDE
    {
        return false;
    }   // notifyEventAsynchronous

};   // class GameEventsProtocol

#endif // GAME_EVENTS_PROTOCOL_HPP
