// ChatClient.cpp
#include "ChatClient.h"
#include "CommandParser.h"
#include "Logger.h"

ChatClient::~ChatClient()
{
	m_isRunning.store(false, std::memory_order_release); // TODO : Memory Barrier: Acquire-Release semantics

	closesocket(m_serverSocket);

	if (m_receiveThread.joinable())
	{
		m_receiveThread.join();
	}

	WSACleanup();

	LOG_INFO("ChatClient") << "연결 종료, 클라이언트 정리 완료.";
}

bool ChatClient::ConnectServer(const std::string& serverIP, unsigned short port)
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr);

	if (connect(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		LOG_ERROR("ChatClient") << "서버 연결 실패: " << WSAGetLastError();
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;

		WSACleanup(); // WSAStartup이 있으니 여기에 Cleanup
		return false;
	}

	m_isRunning.store(true, std::memory_order_release); // Server와 통신 가능 상태
	return true;
}

void ChatClient::Run()
{
	std::cout << "서버에 연결되었습니다." << std::endl;

	// 수신 시작
	m_receiveThread = std::thread(&ChatClient::ReceiveRespond, this);

	// 채팅 입력 
	std::string userInput;
	while (m_isRunning.load(std::memory_order_acquire))
	{
		EState curState = m_state.load(std::memory_order_acquire);

		if (curState == EState::NewLoggingIn)
		{
			while (true)
			{
				std::cout << "사용할 닉네임을 입력하세요: ";
				GetLine(userInput, curState);
				if (IsValidUsername(userInput))
				{
					break;
				}
			}
						
			m_username = userInput;
			ReqLogin(userInput, false);
			continue;
		}
		else if (curState == EState::OldLoggingIn)
		{
			std::cout << "이미 있는 회원 이름입니다. 접속하시겠습니까? (y/n): ";
			GetLine(userInput, curState);
			if (userInput == "y" || userInput == "Y")
			{
				ReqLogin(m_username, true);
			}
			else
			{
				m_state = EState::NewLoggingIn;
			}
			continue;
		}
		else if (curState == EState::WaitingLogIn)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_condition.wait(lock, [this]() {
				return m_state.load(std::memory_order_acquire) != EState::WaitingLogIn;
				});
			continue;
		}

		// 로그인 된 거 보장: 입력을 커맨드로 파싱해 실행한다.
		GetLine(userInput, curState);

		auto command = CommandParser::Parse(userInput, curState);
		if (!command->Execute(*this)) // ExitCommand 등은 false 반환 → 루프 종료
		{
			break;
		}
	}
}

void ChatClient::ReceiveRespond()
{
	char tempBuffer[MAX_BUFFER_SIZE];
	while (m_isRunning.load(std::memory_order_acquire))
	{
		int receiveLength = recv(m_serverSocket, (char*)&tempBuffer, sizeof(tempBuffer), 0);

		if (receiveLength <= 0)
		{
			std::cout << "\n서버와 연결이 끊겼습니다." << std::endl;
			break;
		}

		std::vector<ClientPacket> contexts;
		m_packetAssembler.ProcessBytes(tempBuffer, receiveLength, contexts);

		for (const auto& context : contexts)
		{
			ProcessPacket(context.type, context.body);
		}
	}
}

void ChatClient::GetLine(std::string& outInput, EState& outState)
{
	outInput.clear();

	if (!std::getline(std::cin, outInput))
	{
		return;
	}

	outState = m_state.load(std::memory_order_acquire);

	if (outInput.length() >= MAX_MESSAGE_LEN)
	{
		std::cout << "[오류] 입력이 너무 길어 잘렸습니다." << std::endl;
		outInput.resize(MAX_MESSAGE_LEN - 1);
	}
}

bool ChatClient::IsValidUsername(std::string& username)
{
	size_t spacePos = username.find(' ');
	if (spacePos != std::string::npos)
	{
		std::cout << "닉네임에 공배 사용 불가. 공백 앞부분만 사용됩니다." << std::endl;
		username.erase(spacePos); // 공백 이후 삭제
	}

	if (username.empty())
	{
		std::cout << "[오류] 닉네임이 비었습니다. 다시 입력해주세요." << std::endl;
		return false;
	}

	if (username.length() >= MAX_USERNAME_LEN)
	{
		std::cout << "[오류] 너무 깁니다. " << MAX_USERNAME_LEN - 1 << "자 미만으로 입력!" << std::endl;
		return false;
	}

	return true;
}

void ChatClient::ProcessPacket(EPacketType messageType, std::shared_ptr<IBody> packet)
{
	switch (messageType)
	{
	case EPacketType::LoginRes:
		ResLogin(packet);
		break;

	case EPacketType::RoomCreateRes:
		ResCreateRoom(packet);
		break;

	case EPacketType::RoomJoinRes:
		ResJoinRoom(packet);
		break;

	case EPacketType::ChattingBroadcast:
		ResBroadcastChat(packet);
		break;

	case EPacketType::ChattingPended:
		ResPendedChat(packet);
		break;

	case EPacketType::WhisperDeliveredRes:
		ResWhisperDeliveredRes(packet);
		break;

	case EPacketType::WhisperFailRes:
		ResWhisperFailedRes(); // TODO : fail 원인도 넣어주면 좋을 듯, 그 사라밍 offline 이거나 없는 사람이거나.
		break;

	case EPacketType::RoomlistRes:
		ResRoomList(packet);
		break;

	case EPacketType::UserlistRes:
		ResUserList(packet);
		break;

	default:
		LOG_WARN("ChatClient") << "처리 불가한 패킷 타입: " << (int)messageType;
		break;
	}
}

void ChatClient::ResLogin(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<LoginResBody>(packet);

	if (resBody->isCanLogIn) // TODO: 프로토콜상 거부 상태 세분화 필요
	{
		m_joinedRoomID = resBody->roomID;

		if (resBody->isNew)
		{
			std::cout << "\n새 회원 접속 성공! username: " << m_username << "\n";
		}
		else
		{
			std::cout << "\n기존 회원 접속 성공! username: " << m_username << "\n";
		}

		m_state = (m_joinedRoomID == LOBBY_ID) ? EState::InLobby : EState::InRoom;
	}
	else
	{
		m_state = EState::OldLoggingIn;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_condition.notify_one();
}

void ChatClient::ResCreateRoom(std::shared_ptr<IBody> packet) // 아래 JoinRoom이랑 로직 중복
{
	auto resBody = std::static_pointer_cast<CreateRoomResBody>(packet);

	if (resBody->isNew)
	{
		m_joinedRoomID = resBody->newRoomID;
		m_state = EState::InRoom; // TODO : state 언제 언제 바뀌는지 꼬이지 않게!

		std::cout << "방 만들기 성공! RoomID: " << m_joinedRoomID << "에 참여 중" << std::endl;
	}
	else
	{
		std::cout << "방 만들기 실패!" << std::endl;

		m_joinedRoomID = LOBBY_ID;
		m_lastReadChatIndex = 0;

		m_state = EState::InLobby;
	}
}

void ChatClient::ResJoinRoom(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<JoinRoomResBody>(packet);

	if (resBody->isSuccess)
	{
		m_state = EState::InRoom;

		std::cout << "방에 성공적으로 참여했습니다." << std::endl;
	}
	else
	{
		m_joinedRoomID = LOBBY_ID;
		m_lastReadChatIndex = 0;

		m_state = EState::InLobby;

		std::cout << "방 참여에 실패했습니다. (없거나 인원이 다 찼음)" << std::endl; // TODO : m_rooms에 인원 기록?
	}
}

void ChatClient::ResBroadcastChat(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<ChattingBroadcastBody>(packet);

	m_lastReadChatIndex++;

	if (resBody->sendingUser == m_username) // 내가 보낸 메시지는 다시 받지 않음, 오류 확인용
	{
		// std::cout << "(by me)";
		return;
	}
	std::cout << "[User " << resBody->sendingUser << "]: " << resBody->message << std::endl;
}

void ChatClient::ResPendedChat(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<ChattingBroadcastBody>(packet);

	std::cout << "[User " << resBody->sendingUser << "]: " << resBody->message << std::endl;
}

void ChatClient::ResUserList(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<UserListResBody>(packet);

	std::cout << "--- Live User List (" << resBody->userCount << ") ---" << std::endl;
	for (UINT16 i = 0; i < resBody->userCount; i++)
	{
		std::cout << i << ": " << resBody->users[i] << std::endl;
	}
}

void ChatClient::ResRoomList(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<RoomListResBody>(packet);

	std::cout << "--- Room List (" << resBody->roomCount << ") ---" << std::endl;

	for (UINT16 i = 0; i < resBody->roomCount; i++)
	{
		std::cout << i << ": " << resBody->rooms[i] << std::endl;
	}
}

void ChatClient::ResWhisperDeliveredRes(std::shared_ptr<IBody> packet)
{
	auto resBody = std::static_pointer_cast<WhisperDeliveredResBody>(packet);

	std::cout << "[귓속말 from " << resBody->sendingUser << "]: " << resBody->message << std::endl;
}

void ChatClient::ResWhisperFailedRes()
{
	std::cout << "귓속말 전송 실패: 대상을 찾을 수 없거나 오프라인입니다." << std::endl;
}

void ChatClient::ReqLogin(const std::string& username, bool isOldOkay)
{
	LoginReqBody reqBody;
	reqBody.isOldOkay = isOldOkay;
	strncpy_s(reqBody.username, MAX_USERNAME_LEN, username.c_str(), _TRUNCATE);

	m_state = EState::WaitingLogIn;
	SendPacket<EPacketType::LoginReq>(m_serverSocket, reqBody);
}

void ChatClient::ReqCreateRoom(const std::string& roomTitle)
{
	CreateRoomReqBody reqBody;
	strncpy_s(reqBody.roomname, MAX_ROOM_TITLE_LEN, roomTitle.c_str(), _TRUNCATE);

	SendPacket<EPacketType::RoomCreateReq>(m_serverSocket, reqBody);
}

void ChatClient::ReqJoinRoom(UINT roomID)
{
	JoinRoomReqBody reqBody;
	reqBody.roomID = roomID;
	m_joinedRoomID = roomID; // 참여 요청한 방 ID를 기억

	SendPacket<EPacketType::RoomJoinReq>(m_serverSocket, reqBody);
}

void ChatClient::ReqQuitRoom()
{
	QuitRoomReqBody reqBody;
	reqBody.roomID = m_joinedRoomID;

	SendPacket<EPacketType::RoomQuitReq>(m_serverSocket, reqBody);
	std::cout << "나갑니다..." << std::endl;

	m_joinedRoomID = LOBBY_ID; // TODO : 초기화를 여기서?
	m_lastReadChatIndex = 0;

	m_state = EState::InLobby;
}

void ChatClient::ReqUserList()
{
	UserListReqBody reqBody;

	SendPacket<EPacketType::UserlistReq>(m_serverSocket, reqBody);
	std::cout << "유저 리스트 요청합니다..." << std::endl;
}

void ChatClient::ReqRoomList()
{
	RoomListReqBody reqBody;

	SendPacket<EPacketType::RoomlistReq>(m_serverSocket, reqBody);
	std::cout << "룸 리스트 요청합니다..." << std::endl;
}

void ChatClient::ReqWhisper(std::string_view targetUser, std::string_view message)
{
	WhisperReqBody reqBody;

	if (targetUser.length() > static_cast<size_t>(MAX_USERNAME_LEN - 1)) // TODO : 검증을 좀 더 일찍해도 될 듯?
	{
		std::cout << "User 이름이 너무 깁니다! " << MAX_USERNAME_LEN - 1 << " 글자 제한\n";
		return;
	}

	if (message.length()> static_cast<size_t>(MAX_MESSAGE_LEN - 1)) // TODO : 검증을 좀 더 일찍해도 될 듯?
	{
		std::cout << "메시지가 너무 깁니다! " << MAX_MESSAGE_LEN - 1 << " 글자 제한\n";
		return;
	}

	std::memcpy(reqBody.receivingUser, targetUser.data(), targetUser.length()); // string_view는 .data()로 내부 포인터 접근 가능 (길이 제한 복사)
	reqBody.receivingUser[targetUser.length()] = '\0'; // 뒤 \0 문자 보장

	std::memcpy(reqBody.message, message.data(), message.length());
	reqBody.message[message.length()] = '\0';

	SendPacket<EPacketType::WhisperReq>(m_serverSocket, reqBody);
}

void ChatClient::ReqChat(const std::string& message)
{
	ChattingReqBody reqBody;
	reqBody.roomID = m_joinedRoomID;
	strncpy_s(reqBody.message, MAX_MESSAGE_LEN, message.c_str(), _TRUNCATE);

	SendPacket<EPacketType::ChattingReq>(m_serverSocket, reqBody);
}

void ChatClient::ReqUpdateLastReadChat() // TODO : 곧 이 기능으로 마이그레이션
{
	ChattingConfirmReqBody reqBody;
	reqBody.roomID = m_joinedRoomID;
	reqBody.lastReadIndex = m_lastReadChatIndex;

	SendPacket<EPacketType::ChattingConfirm>(m_serverSocket, reqBody);
}