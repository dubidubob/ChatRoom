#include "Session.h"
#include "Protocol.h"
#include "Logger.h"

void Session::OnRecv(const char* buffer, int len, std::vector<ServerToManagerPacket>& packets)
{
    m_assembler.ProcessBytes(buffer, len, packets);
}