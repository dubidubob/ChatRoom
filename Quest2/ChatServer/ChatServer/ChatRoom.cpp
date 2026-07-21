#include "ChatRoom.h"
#include "ServerInternalTypes.h"

#include <thread>

ChatRoom::~ChatRoom()
{
	m_queueCondition.notify_one();
	if (m_thread.joinable())
	{
		m_thread.join();
	}

	std::cout << "ChatRoom " << m_roomID << " 스레드 정리 완료. 룸 객체 파괴.\n";
}

void ChatRoom::Init(ManagerToRoomPacket user, RoomDeleteCallback callback)
{
	m_terminationCallback = std::move(callback);
	m_thread = std::thread(&ChatRoom::Run, this);

	user.type = EPacketType::RoomJoinReq;
	PushPacket(user);
}

void ChatRoom::PushPacket(const ManagerToRoomPacket& data)
{
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		m_packetQueue.push(data);
	}
	m_queueCondition.notify_one();
}

void ChatRoom::DisconnectedUser(const std::string& username)
{
	std::unique_lock<std::shared_mutex> lock(m_userMutex);
	auto it = m_users.find(username);
	if (it == m_users.end())
	{
		std::cout << "ChatRoom : Disconnected " << username << "없는디\n";
		return;
	}
	it->second.isLive = false;
	it->second.lastReadIndex = m_messageCounter.load(); // TODO : for now!! client가 주는 ack 받기
}

void ChatRoom::ReconnectedUser(const ManagerToRoomPacket& packetContext)
{
	UINT64 lastReadIndex;
	{
		std::unique_lock<std::shared_mutex> lock(m_userMutex);

		auto it = m_users.find(packetContext.username);
		if (it == m_users.end())
		{
			std::cout << "ChatRoom : 없는데 reconnected 래요...\n";
			return;
		}
		it->second.isLive = true;
		it->second.sock = packetContext.sock;
		lastReadIndex = it->second.lastReadIndex;
	}
	
	std::cout << "Chatroom : 유저 참여: " << packetContext.username << std::endl;
	SendUnsendedMessages(packetContext.sock, lastReadIndex); // TODO : pended message를 다 출력하고 실시간 chatting을 받게 해야한다. : islive를 false로 둔 후에~
}

void ChatRoom::Run()
{
	while (m_isRun)
	{
		ManagerToRoomPacket packetContext;
		{
			std::unique_lock<std::mutex> lock(m_queueMutex); // TODO : unique랑 lock guard의 차이
			m_queueCondition.wait(lock, [this] {
				return !m_packetQueue.empty() || !m_isRun;
				});

			if (!m_isRun)
			{
				std::cout << "ChatRoom : 중간 탈출이 있나? \n";
				break;
			}

			packetContext = m_packetQueue.front();
			m_packetQueue.pop();
		}

		ManagePacketQueue(packetContext);
	}
}

void ChatRoom::ManagePacketQueue(ManagerToRoomPacket packetContext)
{
	const std::shared_ptr<IBody>& packet = packetContext.body;

	switch (packetContext.type)
	{
	case EPacketType::LoginReq: // 로그인 하자마자 곧바로 왔다는 뜻이므로		
		break;

	case EPacketType::RoomJoinReq:
		MngJoinRoom(packetContext); // 새 접속자 only
		break;

	case EPacketType::ChattingReq:
		MngChatting(packetContext);
		break;

	case EPacketType::RoomQuitReq:
		MngQuit(packetContext);
		break;
	}
}

void ChatRoom::MngJoinRoom(ManagerToRoomPacket packetContext)
{
	{
		std::unique_lock<std::shared_mutex> lock(m_userMutex);
		auto [it, inserted] = m_users.try_emplace(packetContext.username, UserRoomData{ packetContext.sock, true, 0 }); // TODO : 이게 뭐여?
		if (!inserted)
		{
			std::cout << "ChatRoom : Room " << m_roomID << ": User " << packetContext.username << " 가 들어올 수 없는데?\n";
			return;
		}
	}

	std::cout << "ChatRoom : Room " << m_roomID << ": User " << packetContext.username << " joined.\n";
	BroadcastPacket(packetContext.username, "님이 들어왔습니다.");
}

void ChatRoom::MngChatting(ManagerToRoomPacket packetContext)
{
	auto recvBody = std::static_pointer_cast<ChattingReqBody>(packetContext.body);

	BroadcastPacket(packetContext.username, recvBody->message);
}

void ChatRoom::MngQuit(ManagerToRoomPacket packetContext)
{
	bool userRemoved = false;
	{
		std::unique_lock<std::shared_mutex> lock(m_userMutex);
		if (m_users.erase(packetContext.username) > 0) // TODO : else?
		{
			userRemoved = true;
		}
	}

	if (userRemoved)
	{
		std::cout << "ChatRoom : Room " << m_roomID << ": User " << packetContext.username << " quit.\n";

		BroadcastPacket(packetContext.username, "님이 나가셨습니다.");
	}

	bool shouldTerminate = false;
	{
		std::shared_lock<std::shared_mutex> lock(m_userMutex);
		if (m_users.empty())
		{
			shouldTerminate = true;
		}
	}

	if (shouldTerminate)
	{
		if (m_isRun.exchange(false)) // TODO : 단 한 번만 실행!
		{
			if (m_terminationCallback) // ChatManager에게 해당 객체 삭제 요청
			{
				m_terminationCallback(m_roomID);
			}

			m_queueCondition.notify_one();
		}
	}
}

void ChatRoom::BroadcastPacket(const std::string& sendingUsername, const std::string& message)
{
	UINT64 currentIndex = ++m_messageCounter;

	RoomMessage newMessage{ currentIndex, sendingUsername, message };

	{	
		std::unique_lock<std::shared_mutex> lock(m_messagesMutex);

		if (m_messages.size() < MAX_SAVED_MESSAGE_COUNT)
		{
			m_messages.push_back(std::move(newMessage));
		}
		else
		{
			m_messages[m_endIndex] = std::move(newMessage);
		}

		m_endIndex = (m_endIndex + 1) % MAX_SAVED_MESSAGE_COUNT;
	}

	ChattingBroadcastBody reqBody; // TODO : 함수화? 공용함수... 못 씀 ㅜㅜ
	strncpy_s(reqBody.sendingUser, MAX_USERNAME_LEN, sendingUsername.c_str(), _TRUNCATE);
	strncpy_s(reqBody.message, MAX_MESSAGE_LEN, message.c_str(), _TRUNCATE);

	PacketHeader broadcastingPacketHeader;
	broadcastingPacketHeader.type = EPacketType::ChattingBroadcast;
	broadcastingPacketHeader.bodyLength = sizeof(ChattingBroadcastBody);

	std::shared_lock<std::shared_mutex> lock(m_userMutex);
	for (const auto& it : m_users)
	{
		UserRoomData userData = it.second;
		if (userData.isLive)
		{
			SendPacket<ChattingBroadcastBody>(userData.sock, broadcastingPacketHeader, reqBody);
		}
	}
}

void ChatRoom::SendUnsendedMessages(SOCKET sock, UINT lastReadIndex)
{
	std::shared_lock<std::shared_mutex> lock(m_messagesMutex);

	int curSize = m_messages.size();
	int startIndex = 0;
	if (curSize == MAX_SAVED_MESSAGE_COUNT)
	{
		startIndex = m_endIndex; // m_endIndex가 가장 오래된 메시지를 가리킴
	}

	for (int i = 0; i < curSize; ++i)
	{
		int bufferIndex = (startIndex + i) % curSize;
		const auto& msg = m_messages[bufferIndex];

		if (msg.index > lastReadIndex)
		{
			ChattingBroadcastBody resBody;
			strncpy_s(resBody.sendingUser, MAX_USERNAME_LEN, msg.username.c_str(), _TRUNCATE);
			strncpy_s(resBody.message, MAX_MESSAGE_LEN, msg.message.c_str(), _TRUNCATE); 
			
			SendPacket(sock, EPacketType::ChattingBroadcast, resBody);
		}
	}
}