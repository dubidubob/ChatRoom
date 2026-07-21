#pragma once
#include <vector>
#include <string>
#include <memory>

#include "ServerInternalTypes.h"
#include "PacketAssembler.h"

// [수정] username 없애고, 온전히 packet -> body 조립용 클래스
class Session
{
public:
    Session()
        : m_assembler(&Session::CreateServerBody) {}; 

    void OnRecv(const char* buffer, int len, std::vector<ServerToManagerPacket>& packets);

private:
    static std::shared_ptr<IBody> CreateServerBody(const PacketHeader* header); // static : 상태 무관 함수라

private:
    PacketAssembler<ServerToManagerPacket> m_assembler;
};