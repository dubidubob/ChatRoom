// ChatServer.cpp
#include <thread>

#include "ChatServer.h"
#include "Protocol.h"

#pragma comment(lib, "ws2_32.lib")

void ChatServer::Init()
{
	std::thread chatManagerThread(&ChatManager::Run, m_chatManager);
	chatManagerThread.detach(); // fire and forget <-> join()
}

void ChatServer::RunServer()
{
	InitializeServer();
	NetworkLoop(); // TODO : end 조건 제작

	std::cin.get();
	EndServer();
}

void ChatServer::InitializeServer()
{
	// 윈도우 네트워크 기능
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORTNUM);
	serverAddr.sin_addr.s_addr = INADDR_ANY; // 이 컴퓨터에 할당된 모든 IP 주소로부터의 접속 허용

	bind(m_listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(m_listenSock, SOMAXCONN); // 소켓 상태를 들을 수 있는 상태로 변경: 설정 함수

	std::cout << "ChatServer: 서버가 " << PORTNUM << " 포트에서 대기 중..." << std::endl;
}

void ChatServer::NetworkLoop()
{
	while (true)
	{
		fd_set reads; // socket 모음
		FD_ZERO(&reads); // socket 초기화
		FD_SET(m_listenSock, &reads); //  감시 대상 1: 새로운 클라이언트 접속 // select 방식의 한계 : 매번 이렇게 순회하면서 넣어줘야 하나?

		{
			for (const auto& pair : m_sockets)
			{
				FD_SET(pair.first, &reads);	 // 감시 대상 2: 기존 클라이언트들의 메시지
			}
		}

		int selectResult = select(0, &reads, nullptr, nullptr, nullptr);
		if (selectResult == SOCKET_ERROR)
		{
			std::cout << "ChatServer: SOCKET_ERROR : 여기 걸릴라나?\n";
			break;
		}

		//  새로운 클라이언트 접속
		if (FD_ISSET(m_listenSock, &reads))
		{
			SOCKET clientSock = accept(m_listenSock, nullptr, nullptr);
			if (clientSock != INVALID_SOCKET)
			{
				std::cout << "ChatServer: 새 클라이언트 접속: Socket " << clientSock << std::endl;

				m_sockets[clientSock] = std::make_shared<Session>();
			}
			else
			{
				std::cout << "ChatServer: INVALID_SOCKET : 여기 걸릴라나?\n";
			}
		}

		char tempBuffer[MAX_BUFFER_SIZE];
		std::vector<std::pair<SOCKET, std::shared_ptr<Session>>> activeSession;
		{
			for (const auto& pair : m_sockets)
			{
				if (FD_ISSET(pair.first, &reads))
				{
					activeSession.push_back(pair);
				}
			}
		}

		std::vector<SOCKET> disconnectedSockets;
		for (const auto& pair : activeSession)
		{
			SOCKET sock = pair.first;
			int recvLen = recv(sock, tempBuffer, sizeof(tempBuffer), 0); // recv나  send는 블로킹 I/O 함수기 때문에 lock 안에 두지 않는다

			if (recvLen == 0) // 정상 종료
			{
				disconnectedSockets.push_back(sock);
				continue;
			}
			else if (recvLen < 0) 
			{
				int error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK) 
				{
					std::cout << "ChatServer: 수신 버퍼에 데이터가 없음!" << std::endl;
				}
				else 
				{
					// std::cout << "ChatServer: 연결 오류, 끊김" << std::endl; // 비정상 종료
					disconnectedSockets.push_back(sock);
				}
				continue;
			}

			std::vector<ServerToManagerPacket> contexts;
			pair.second->OnRecv(tempBuffer, recvLen, contexts);

			for (auto& context : contexts)
			{
				context.sock = pair.first;

				m_chatManager->PushMessageQueue(std::move(context));
			}
		}
			
		// 연결 끊긴 소켓들 정리
		if (!disconnectedSockets.empty())
		{
			for (SOCKET sock : disconnectedSockets)
			{
				closesocket(sock);
				m_sockets.erase(sock);

				// ChatManager에게 접속 종료 알림
				ServerToManagerPacket packet(sock, EPacketType::UserDisconnectedReq);
				m_chatManager->PushMessageQueue(std::move(packet)); // TODO : 비정상 종료는 곧바로 알려야하지 않나? lock free 하게 하려면 이렇게 하는 거지만..
			}
		}
	}
}

void ChatServer::EndServer()
{
	closesocket(m_listenSock);
	WSACleanup();
}