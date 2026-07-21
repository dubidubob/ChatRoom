// ChatManager.cpp
#include "ChatManager.h"

#include <memory>

void ChatManager::Run()
{
	while (true)
	{
		ServerToManagerPacket packetContext;
		{
			std::cout << "ChatManager : 저 잡니다? \n";
			std::unique_lock<std::mutex> lock(m_queueMutex);
			m_condition.wait(lock, [this] { return !m_clientMessageQueue.empty(); }); // 큐 비었으면(람다식이 false를 반환하면) lock 풀고+스레드 잔다 // notify 오면 lock 잠그고, 깨서 람다 재평가 // TODO : 세부 동작 방식 공부
			std::cout << "ChatManager : 저 깹니다? (잡니다 없으면 데드락) \n";

			packetContext = m_clientMessageQueue.front();
			m_clientMessageQueue.pop();
		}

		ManageMessageQueue(packetContext);
	}
}

void ChatManager::PushMessageQueue(ServerToManagerPacket packetContext)
{
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		m_clientMessageQueue.push(std::move(packetContext));
	}

	m_condition.notify_one();
}

void ChatManager::ManageMessageQueue(ServerToManagerPacket packetContext) // TODO : Manage냐 Process냐
{
	const std::shared_ptr<IBody>& packet = packetContext.body;

	switch (packetContext.type)
	{
	case EPacketType::LoginReq:
		MngLoginClient(packetContext);
		break;

	case EPacketType::RoomCreateReq:
		MngCreateRoom(packetContext);
		break;

	case EPacketType::RoomJoinReq:
		MngJoinRoom(packetContext);
		break;

	case EPacketType::ChattingReq:
		MngChatting(packetContext);
		break;

	case EPacketType::RoomQuitReq:
		MngQuit(packetContext);
		break;

	case EPacketType::UserDisconnectedReq:
		MngDisconnectUser(packetContext);
		break;

	case EPacketType::WhisperReq:
		MngWhisper(packetContext);
		break;

	case EPacketType::UserlistReq:
		MngUserList(packetContext);
		break;

	case EPacketType::RoomlistReq:
		MngRoomList(packetContext);
		break;

	case EPacketType::RoomDeleteInternal:
		{
			auto body = std::static_pointer_cast<RoomDeleteInternalBody>(packetContext.body);
			std::cout << "ChatManager: Room " << body->roomID << " 정리 작업 시작.\n";
			m_rooms.erase(body->roomID);
		}
		break;
	}
}

void ChatManager::MngLoginClient(ServerToManagerPacket packetContext)
{
	auto reqBody = std::static_pointer_cast<LoginReqBody>(packetContext.body);
	bool isOldUserOkay(reqBody->isOldOkay);
	std::string username(reqBody->username);
	SOCKET sock = packetContext.sock;	

	bool isNewUser = false;
	int joinedRoomNum = LOBBY_ID;
	if (auto curUserData = m_users.find(username); curUserData == m_users.end()) // 신규 유저 추가 로직
	{
		isNewUser = true;

		m_users[username] = UserData(LOBBY_ID, sock);
		m_socketToUsername[sock] = username;

		std::cout << "ChatServer : 유저 이름 " << username << "으로 업데이트!\n";
	}
	else if (isOldUserOkay) // 기존 회원에 대한 클라이언트 접속 의사 확인
	{
		if (curUserData->second.sock == INVALID_SOCKET)
		{
			m_users[username].sock = sock;
			m_socketToUsername[sock] = username;

			joinedRoomNum = curUserData->second.joinedRoomID;
		}
		else // 서버 기존 유저가 이미 활성화됐기 땜시 접속 비허용
		{
			isOldUserOkay = false;
		}
	}

	LoginResBody resBody;
	resBody.isNew = isNewUser;

	if (isNewUser)
	{
		resBody.isCanLogIn = true;
		std::cout << "ChatManager : 신규 유저 회원 가입 : " << username << "\n";
	}
	else if (isOldUserOkay) // 기존 유저 접속 허용 + room number 줌
	{
		resBody.isCanLogIn = true;
		std::cout << "ChatManager : 기존 유저 접속 완료 : " << username << "\n";
	}
	else // 기존 유저 아직 허용 안 함
	{
		resBody.isCanLogIn = false;
		std::cout << "ChatManager : 기존 유저 접속 시도 : " << username << "\n";
	}

	resBody.roomID = joinedRoomNum;

	SendPacket(packetContext.sock, EPacketType::LoginRes, resBody);

	if (isOldUserOkay && joinedRoomNum!=LOBBY_ID) // 기존 접속자는 곧바로 방 안으로 보낸다
	{
		ManagerToRoomPacket roomPacket(packetContext, std::move(username));
		if (auto it = m_rooms.find(joinedRoomNum); it != m_rooms.end())
		{
			it->second->ReconnectedUser(roomPacket); // TODO : Sendpacket을 ChatManager도 하고, ChatRoom도 하는데 두 패킷이 섞일 것 같은데 이걸 어떡하지?
		}
	}	
}

void ChatManager::MngCreateRoom(ServerToManagerPacket packetContext)
{
	auto reqBody = std::static_pointer_cast<CreateRoomReqBody>(packetContext.body);

	std::string roomname(reqBody->roomname);

	// 중복 여부 검사
	bool isNewRoom = true;
	for (const auto& room : m_rooms)
	{
		if (room.second->GetRoomname() == roomname)
		{
			if (room.second->IsAlive())
			{
				isNewRoom = false;
				break;
			}
		}
	}

	UINT roomID = LOBBY_ID;
	if (isNewRoom)
	{
		// Room ID 발급
		roomID = m_nextRoomID.fetch_add(1);

		std::string username;
		if (FindUsername(packetContext.sock, username))
		{
			m_users[username].joinedRoomID = roomID;

			// ChatRoom의 삭제 콜백함수
			RoomDeleteCallback callback = [this](UINT deletingRoomID) {
				ServerToManagerPacket packet(EPacketType::RoomDeleteInternal);

				auto body = std::make_shared<RoomDeleteInternalBody>();
				body->roomID = deletingRoomID;
				packet.body = body;

				this->PushMessageQueue(packet);
				};

			// Room 객체 Create + Thread 생성
			auto newRoom = std::make_shared<ChatRoom>(roomID, roomname);
			newRoom->Init(ManagerToRoomPacket(packetContext, username), callback);
			m_rooms[roomID] = newRoom;

			std::cout << "ChatManager : 새 채팅방 생성: " << roomname << " (ID: " << roomID << ")" << std::endl;
		}
	}

	CreateRoomResBody resBody;
	resBody.isNew = isNewRoom;
	if (isNewRoom)
	{
		resBody.newRoomID = roomID;
	}

	SendPacket(packetContext.sock, EPacketType::RoomCreateRes, resBody);
}

void ChatManager::MngJoinRoom(ServerToManagerPacket packetContext)
{
	auto reqBody = std::static_pointer_cast<JoinRoomReqBody>(packetContext.body);

	std::string username = m_socketToUsername[packetContext.sock];
	
	UINT roomID(reqBody->roomID);
	bool isSuccess = CanJoinRoom(roomID);
	if (isSuccess)
	{
		m_users[username].joinedRoomID = roomID;
		SendToRoom(roomID, packetContext);
		std::cout << "ChatManager : 유저 참여: " << username << std::endl;
	}

	JoinRoomResBody resBody;
	resBody.isSuccess = isSuccess;

	SendPacket(packetContext.sock, EPacketType::RoomJoinRes, resBody);
}

void ChatManager::MngChatting(ServerToManagerPacket packetContext)
{
	auto reqBody = std::static_pointer_cast<ChattingReqBody>(packetContext.body);

	UINT roomID(reqBody->roomID);

	SendToRoom(roomID, packetContext); // TODO : 미전송 처리?
}

void ChatManager::MngQuit(ServerToManagerPacket packetContext)
{
	auto reqBody = std::static_pointer_cast<QuitRoomReqBody>(packetContext.body);

	UINT roomID(reqBody->roomID);

	SendToRoom(roomID, packetContext);
	
	m_users[m_socketToUsername[packetContext.sock]].joinedRoomID = LOBBY_ID;
}

void ChatManager::MngDisconnectUser(ServerToManagerPacket packetContext)
{
	// sock에 해당 됐던 user 비활성화시키기
	UINT joinRoomID = LOBBY_ID;
	std::string username;
	if (FindUsername(packetContext.sock, username))
	{
		if (auto userdata = m_users.find(username); userdata != m_users.end())
		{
			userdata->second.sock = INVALID_SOCKET;		// m_users : 비정상 종료이므로 유저 비활성화만
			joinRoomID = userdata->second.joinedRoomID; //유저가 들어있는 방 담기
		}
		m_socketToUsername.erase(packetContext.sock); // m_socketToUsers : sock 필요없으므로, sock-user 삭제
	}

	if (joinRoomID != LOBBY_ID) // 해당 방에게도 알려주기
	{
		if (auto it = m_rooms.find(joinRoomID); it != m_rooms.end())
		{
			it->second->DisconnectedUser(username);
		}
	}

	std::cout << "ChatServer: 클라이언트 접속 끊김: User: " << username << std::endl;
}

void ChatManager::MngUserList(ServerToManagerPacket packetContext)
{
	UserListResBody resBody;
	resBody.userCount = 0;

	for (const auto& pair : m_users)
	{
		if (resBody.userCount >= MAX_LIST_COUNT)
		{
			std::cout << "ChatManager: " << MAX_LIST_COUNT << "를 넘어갑니다. 자릅니다. \n";
			break; // TODO : 오버플로우 명시적 방지
		}

		if (pair.second.sock != INVALID_SOCKET)
		{
			strncpy_s(resBody.users[resBody.userCount], MAX_USERNAME_LEN, pair.first.c_str(), _TRUNCATE);
			resBody.userCount++;
		}
	}

	SendPacket(packetContext.sock, EPacketType::UserlistRes, resBody);
}

void ChatManager::MngRoomList(ServerToManagerPacket packetContext)
{
	RoomListResBody resBody;
	resBody.roomCount = 0;

	for (const auto& pair : m_rooms)
	{
		if (resBody.roomCount >= MAX_LIST_COUNT)
		{
			std::cout << "ChatManager: " << MAX_LIST_COUNT << "를 넘어갑니다. 자릅니다. \n";
			break; // TODO : 오버플로우 명시적 방지
		}

		if (pair.second->IsAlive())
		{
			resBody.rooms[resBody.roomCount] = pair.first;
			resBody.roomCount++;
		}
	}

	SendPacket(packetContext.sock, EPacketType::RoomlistRes, resBody);
}

void ChatManager::MngWhisper(ServerToManagerPacket packetContext)
{
	auto reqBody = std::static_pointer_cast<WhisperReqBody>(packetContext.body);

	std::string message = reqBody->message;

	bool isSuccess = false;
	SOCKET receivingSock;
	
	if (auto it = m_users.find(reqBody->receivingUser); it != m_users.end())
	{
		const auto& user = it->second;
		if (user.sock != INVALID_SOCKET)
		{
			isSuccess = true;
			receivingSock = user.sock;
		}
	}

	if (isSuccess)
	{
		std::string sendingUsername;
		if (FindUsername(packetContext.sock, sendingUsername))
		{
			WhisperDeliveredResBody resBody;
			strncpy_s(resBody.sendingUser, MAX_USERNAME_LEN, sendingUsername.c_str(), _TRUNCATE);
			strncpy_s(resBody.message, MAX_MESSAGE_LEN, reqBody->message, _TRUNCATE);

			std::cout << "ChatManager : 귓말 보냈다구욧!\n";
			SendPacket(receivingSock, EPacketType::WhisperDeliveredRes, resBody);
		}	
	}
	else
	{
		WhisperFailedResBody resBody;
		std::cout << "ChatManager : 귓말 실패했다구욧!!\n";
		SendPacket(packetContext.sock, EPacketType::WhisperFailRes, resBody); // send back
	}
}

void ChatManager::SendToRoom(UINT roomID, const ServerToManagerPacket& packetContext)
{
	std::string username;
	if (FindUsername(packetContext.sock, username))
	{
		auto it = m_rooms.find(roomID);
		if (it != m_rooms.end())
		{
			it->second->PushPacket(ManagerToRoomPacket(packetContext, username));
		}
		else
		{
			std::cout << "ChatManager : room이 없음!\n";
		}
	}
}

bool ChatManager::CanJoinRoom(UINT roomID)
{
	auto it = m_rooms.find(roomID);

	if (it == m_rooms.end() || !it->second->IsAlive())	// 1. 룸 없거나, 없어져야 하면 join 못함
	{
		std::cout << "ChatManager : Join이 안 돼! Room ID가 없삼!\n";
		return false;
	}

	if (it->second->IsRoomFull())						// 2. 룸에 인원 다 찼으면 join 못함
	{
		std::cout << "ChatManager : SendToRoom이 안 돼! 인원 다 참!\n";
		return false;
	}

	return true;
}

bool ChatManager::FindUsername(SOCKET sock, std::string& outUsername)
{
	if (auto it = m_socketToUsername.find(sock); it != m_socketToUsername.end())
	{
		outUsername = it->second;
		return true;
	}

	std::cout << "해당 sock의 username 없삼!\n";
	return false;
}