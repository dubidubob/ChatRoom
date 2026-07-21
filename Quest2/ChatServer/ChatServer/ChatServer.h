// ChatServer.h
#pragma once
#include <winsock2.h>
#include <shared_mutex>
#include <unordered_map>
#include <string>

#include "ChatManager.h"
#include "Session.h"

/*
* ChatServer가 ChatManager를 소유함
* 그러나, ChatManager는 thread detach로 Chatserver가 끝나도 돌 수 있는 구조
* 
* ChatServer : socket만 안다
* ChatManager : socket - username 매핑
* ChatRoom : username으로 안다
* 이렇게 바꾼 이유 : ChatServer의 Lock Free 구조를 위해서. 원래는 ChatManager가 Chatserver에 있는 username을 업데이트 시켜야 해서 ChatServer쪽에 mutex가 있었다.
*/

class ChatServer
{
public:
	ChatServer() : m_chatManager(std::make_shared<ChatManager>()) {}

	void Init();
	void RunServer();

private:
	void InitializeServer();
	void NetworkLoop();
	void EndServer();

private:
	std::shared_ptr<ChatManager> m_chatManager = nullptr; // TODO : 충분히 mananger 취급을 하고 있나?

	SOCKET m_listenSock = INVALID_SOCKET;

	std::unordered_map<SOCKET, std::shared_ptr<Session>> m_sockets;
};