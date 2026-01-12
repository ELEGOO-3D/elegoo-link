#ifndef WEBSOCKET_PROTOCOL_H
#define WEBSOCKET_PROTOCOL_H

#include "protocols/websocket_base.h"

namespace elink
{
    class WebSocketProtocol : public WebSocketBase
    {
    public:
        WebSocketProtocol();
        virtual ~WebSocketProtocol() = default;

        std::string getProtocolType() const override { return "websocket"; }
    };
}

#endif // WEBSOCKET_PROTOCOL_H
