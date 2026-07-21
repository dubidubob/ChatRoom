// serverMain.cpp
#include "ChatServer.h"

int main()
{
	std::shared_ptr<ChatServer> chatServer = std::make_shared<ChatServer>();
	chatServer->Init();
	chatServer->RunServer();

	return 0;
}