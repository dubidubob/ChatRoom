#include "Session.h"
#include "Protocol.h"

void Session::OnRecv(const char* buffer, int len, std::vector<ServerToManagerPacket>& packets)
{
    m_assembler.ProcessBytes(buffer, len, packets);
}

std::shared_ptr<IBody> Session::CreateServerBody(const PacketHeader* header)
{
    // 패킷 처리 시작 
    const char* bodyData = reinterpret_cast<const char*>(header) + sizeof(PacketHeader); // TODO : const char* bodyData = m_recvBuffer.data() + sizeof(PacketHeader);

    switch (header->type)
    {
    case EPacketType::LoginReq:
        return CreateBody<LoginReqBody>(bodyData, header->bodyLength);

    case EPacketType::RoomCreateReq:
        return CreateBody<CreateRoomReqBody>(bodyData, header->bodyLength);

    case EPacketType::RoomJoinReq:
        return CreateBody<JoinRoomReqBody>(bodyData, header->bodyLength);

    case EPacketType::ChattingReq:
        return CreateBody<ChattingReqBody>(bodyData, header->bodyLength);

    case EPacketType::RoomQuitReq:
        return CreateBody<QuitRoomReqBody>(bodyData, header->bodyLength);

    case EPacketType::WhisperReq:
        return CreateBody<WhisperReqBody>(bodyData, header->bodyLength);

    case EPacketType::UserlistReq:
        return CreateBody<UserListReqBody>(bodyData, header->bodyLength); // TODO : 굳이 구조체? 일관성?

    case EPacketType::RoomlistReq:
        return CreateBody<RoomListReqBody>(bodyData, header->bodyLength);

    default:
        std::cout << "Session : 이상한 타입이 옴 : " << static_cast<int>(header->type) << std::endl;
        return nullptr;
    }
}