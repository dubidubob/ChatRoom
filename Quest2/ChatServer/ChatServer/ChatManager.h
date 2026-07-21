//ChatManager.h
#pragma once
#include <unordered_map>
#include <queue>
#include <string>
#include <mutex>
#include <shared_mutex>

#include "Protocol.h"		// Network Packet
#include "ServerInternalTypes.h"	// Server Packet

#include "ChatRoom.h"

/*
* Thread 1 - 1 - N 구조
*
* 1. Server Thread : Server 객체 만드는 Thread 하나: Client Msg를 Queue에 넣어줌 (ChatServer.cpp)
* 2. ChatManager Thread: Chat관련 명령어 관리하는 Thread 하나 : /login ...(ServerLogic.h)
* 3. Room Thread : 채팅방에서 메시지 Broadcast하는 Thread N개 : Room 당 하나의 Thread
*
* Mutex가 없는 이유
* 
* 1. ChatManager는 현재 싱글 스레드
* 2. ChatRoom은 ChatManager를 참조하지 않는다.
* 3. ChatManager를 소유한 ChatServer는 오직 PushMessageQueue 함수를 통해서만 ChatManager와 연결된다
* => Queue를 제외한 다른 자원은 lock free 가능
*/

class ChatManager
{
public:
	void Run();

	void PushMessageQueue(ServerToManagerPacket packet);

private:
	void ManageMessageQueue(ServerToManagerPacket packet);

	void MngLoginClient(ServerToManagerPacket packet);
	void MngCreateRoom(ServerToManagerPacket packet);
	void MngJoinRoom(ServerToManagerPacket packet);
	void MngChatting(ServerToManagerPacket packet);
	void MngQuit(ServerToManagerPacket packet);
	void MngDisconnectUser(ServerToManagerPacket packet); // chat server에서 비정상 종료를 알림
	void MngUserList(ServerToManagerPacket packet);
	void MngRoomList(ServerToManagerPacket packet);
	void MngWhisper(ServerToManagerPacket packet);

	// Helpers
	void SendToRoom(UINT roomID, const ServerToManagerPacket& packet);
	bool CanJoinRoom(UINT roomID);

	bool FindUsername(SOCKET sock, std::string& outUsername);

private:
	struct UserData
	{
		UINT joinedRoomID = LOBBY_ID;
		SOCKET sock = INVALID_SOCKET;

		UserData() = default;
		UserData(UINT inID, SOCKET inSock) : joinedRoomID(inID), sock(inSock) {};
	};

	// User
	std::unordered_map< SOCKET, std::string> m_socketToUsername;	// 언제나 바뀜
	std::unordered_map<std::string, UserData> m_users;				// 영구히 바뀌지 않는 정보

	// ChatRoom
	std::unordered_map<UINT, std::shared_ptr<ChatRoom>> m_rooms;// 각 룸의 정보, 룸 당 하나의 thread

	// Data
	std::mutex m_queueMutex;
	std::condition_variable m_condition;
	std::queue<ServerToManagerPacket> m_clientMessageQueue; // 처리해야할 큐

	// ID 발급기
	std::atomic<UINT> m_nextRoomID{ 1 };
};