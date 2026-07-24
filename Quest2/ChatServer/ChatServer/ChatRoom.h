#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <memory>

#include "Protocol.h"
#include "ServerInternalTypes.h"
#include "SafeQueue.h"

class ChatRoom
{
public:
	ChatRoom(UINT roomID, const std::string& roomname)
		:m_roomID(roomID), m_roomname(roomname) {};
	~ChatRoom();

	void Init(ManagerToRoomPacket user, RoomDeleteCallback callback);

	void PushPacket(const ManagerToRoomPacket& packetContext);

	const std::string& GetRoomname() { return m_roomname; }

	bool IsAlive() { return m_isRun; }
	bool IsRoomFull() 
	{
		std::shared_lock<std::shared_mutex> lock(m_userMutex);
		return (m_users.size() >= MAX_ROOM_USER);
	}

	void DisconnectedUser(const std::string& username); // 비정상 종료, 정상 종료는 MngQuit
	void ReconnectedUser(const ManagerToRoomPacket& packetContext);

private:
	void Run();

	void ManagePacketQueue(ManagerToRoomPacket packetContext);
	void MngJoinRoom(ManagerToRoomPacket packetContext);
	void MngChatting(ManagerToRoomPacket packetContext);
	void MngQuit(ManagerToRoomPacket packetContext);

	void BroadcastPacket(const std::string& sendingUsername, const std::string& message);
	void SendUnsendedMessages(SOCKET sock, UINT lastReadIndex);

private:
	struct RoomMessage
	{
		UINT64 index = 0;
		std::string username = "";
		std::string message = "";
	};

	struct UserRoomData
	{
		SOCKET sock = INVALID_SOCKET;
		bool isLive = false;
		UINT64 lastReadIndex = 0;
	};

	RoomDeleteCallback m_terminationCallback = nullptr; // 콜백 함수 멤버

	UINT m_roomID = 0;
	std::string m_roomname = "";

	std::shared_mutex m_userMutex; // 이후 const 안에서 쓴다면 mutable 필요
	std::unordered_map<std::string, UserRoomData> m_users;

	std::thread m_thread;
	std::atomic<bool> m_isRun = true;

	SafeQueue<ManagerToRoomPacket> m_queue;

	std::shared_mutex m_messagesMutex;
	std::vector<RoomMessage> m_messages; 
	int m_endIndex = 0;		// 위 링 버퍼의 끝 인덱스
	std::atomic<UINT64> m_messageCounter{ 0 };
};