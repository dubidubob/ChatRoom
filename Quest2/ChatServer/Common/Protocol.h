// protocol.h
#pragma once
#include <cstdint>
#include <iostream>
#include <ws2tcpip.h>
#include <WinSock2.h>
#include <string_view>

#if defined(_MSC_VER) // TODO 매크로 공부
#define PRETTY_FUNCTION_MACRO __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#define PRETTY_FUNCTION_MACRO __PRETTY_FUNCTION__
#else
#define PRETTY_FUNCTION_MACRO __func__ // Fallback
#endif

// 클라이언트와 서버가 통신 규약을 정하는 곳
// TODO : 여기 contexpr들 정리 및 숫자에 근거 달기
constexpr int MAX_USERNAME_LEN = 32; // TODO : 제한시키는 거 만들기, 매번 32개 왔다갔다 시키지 말기!
constexpr int MAX_ROOM_TITLE_LEN = 32;
constexpr int MAX_MESSAGE_LEN = 128;
constexpr int MAX_SAVED_MESSAGE_COUNT = 128;

constexpr int LOBBY_ID = 0;

constexpr size_t MAX_PACKET_BODY_LENGTH = 65536; // TODO : 64kb 왜 이 숫자? u short이자 port 번호, 이렇게 많아질 수 없긴 하지만 혹시 모르니까 한 번 자르기
constexpr UINT MAX_BUFFER_SIZE = 4096;  // TODO : tempBuffer 너무 큰데/ ibody 상속체 중 가장 큰 거 기준?
constexpr UINT16 MAX_LIST_COUNT = 50;
constexpr UINT16 MAX_ROOM_USER = 10;
constexpr u_short PORTNUM = 8080;
constexpr char IP[] = "127.0.0.1";

enum class EPacketType : uint8_t // TODO : 각 타입 하나하나 주석 달기
{
	LoginReq,
	LoginRes,

	RoomCreateReq,
	RoomCreateRes,

	RoomJoinReq,
	RoomJoinRes,

	RoomQuitReq,
	UserDisconnectedReq,

	UserlistReq,
	UserlistRes,

	RoomlistReq,
	RoomlistRes,

	ChattingReq,
	ChattingBroadcast,
	ChattingPended,
	ChattingConfirm,

	WhisperReq,
	WhisperFailRes,
	WhisperDeliveredRes,

	RoomDeleteInternal,

	None,
};

// Structs================================================

#pragma pack(push, 1)
struct PacketHeader
{
	UINT16 bodyLength = 0; // Body의 길이
	EPacketType type = EPacketType::None;
};

struct IBody 
{
	// virtual ~IBody() = default; // TODO : make_shared로 생성 중이지만 -> POD라 필요없음
};

struct LoginReqBody : public IBody
{
	char username[MAX_USERNAME_LEN] = { 0 };
	bool isOldOkay = false; // TODO : Login Confirm용으로 하나 만들까?
};

struct LoginResBody : public IBody
{
	bool isCanLogIn = false;
	bool isNew = false;
	UINT roomID = LOBBY_ID;
};

struct CreateRoomReqBody : public IBody
{
	char roomname[MAX_ROOM_TITLE_LEN] = { 0 };
};

struct CreateRoomResBody : public IBody
{
	bool isNew = false;
	UINT newRoomID = LOBBY_ID;
};

struct JoinRoomReqBody : public IBody
{
	UINT roomID = LOBBY_ID;
};

struct JoinRoomResBody : public IBody
{
	bool isSuccess = false;
};

struct ChattingReqBody : public IBody
{
	UINT roomID = LOBBY_ID;
	char message[MAX_MESSAGE_LEN] = { 0 };
};

struct ChattingBroadcastBody : public IBody // TODO : room ID가 필요할까?
{
	char sendingUser[MAX_USERNAME_LEN] = { 0 };
	char message[MAX_MESSAGE_LEN] = { 0 };
};

struct ChattingConfirmReqBody : public IBody
{
	UINT roomID = LOBBY_ID;
	UINT lastReadIndex = 0;
};

struct QuitRoomReqBody : public IBody
{
	UINT roomID = LOBBY_ID;
};

struct WhisperReqBody : public IBody
{
	char receivingUser[MAX_USERNAME_LEN] = { 0 };
	char message[MAX_MESSAGE_LEN] = { 0 };
};

struct WhisperFailedResBody : public IBody
{
};

struct WhisperDeliveredResBody : public IBody
{
	char sendingUser[MAX_USERNAME_LEN] = { 0 };
	char message[MAX_MESSAGE_LEN] = { 0 };
};

struct UserListReqBody : public IBody
{
};

struct UserListResBody : public IBody
{
	UINT16 userCount = 0;
	char users[MAX_LIST_COUNT][MAX_USERNAME_LEN] = { 0 };
};

struct RoomListReqBody : public IBody
{
};

struct RoomListResBody : public IBody
{
	UINT16 roomCount = 0;
	UINT rooms[MAX_LIST_COUNT] = { 0 };
};

struct RoomDeleteInternalBody : public IBody
{
	UINT roomID = LOBBY_ID;
};

#pragma pack(pop)

// Helpers================================================s
template<typename ConcreteIBody>
void SendPacket(SOCKET sock, PacketHeader header, const ConcreteIBody& body)
{
	const int totalLength = sizeof(PacketHeader) + sizeof(ConcreteIBody);
	char sendBuffer[totalLength];

	memcpy(sendBuffer, &header, sizeof(PacketHeader));
	memcpy(sendBuffer + sizeof(PacketHeader), &body, sizeof(ConcreteIBody));
	send(sock, sendBuffer, totalLength, 0); // TODO : 반환값 체크하기!
}

template<typename ConcreteIBody>
void SendPacket(SOCKET sock, EPacketType messageType, const ConcreteIBody& body)
{
	PacketHeader header;
	header.type = messageType;
	header.bodyLength = static_cast<UINT16>(sizeof(ConcreteIBody));

	SendPacket(sock, header, body);
}

template<typename ConcreteIBody>
std::shared_ptr<IBody> CreateBody(const char* rawData, UINT16 bodyLength)
{
	if (bodyLength != sizeof(ConcreteIBody))
	{
		std::cout << "ChatManager : " << PRETTY_FUNCTION_MACRO << ": Packet Length 불일치\n";
		return nullptr;
	}

	const ConcreteIBody* rawBody = reinterpret_cast<const ConcreteIBody*>(rawData);
	return std::make_shared<ConcreteIBody>(*rawBody); // 제대로 된 vptr로 초기화 // TODO : 힙 복사 이슈+vptr 바이트까지 전송됨  shared ptr이냐 raw ptr+memcpy이냐... => raw pointer?
}