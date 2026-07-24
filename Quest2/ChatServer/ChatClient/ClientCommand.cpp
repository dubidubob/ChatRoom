// ClientCommand.cpp
#include "ClientCommand.h"
#include "ChatClient.h"

#include <iostream>

bool CreateRoomCommand::Execute(ChatClient& client)
{
	client.ReqCreateRoom(m_title);
	return true;
}

bool JoinRoomCommand::Execute(ChatClient& client)
{
	client.ReqJoinRoom(m_roomID);
	return true;
}

bool LeaveRoomCommand::Execute(ChatClient& client)
{
	client.ReqQuitRoom();
	return true;
}

bool RoomListCommand::Execute(ChatClient& client)
{
	client.ReqRoomList();
	return true;
}

bool UserListCommand::Execute(ChatClient& client)
{
	client.ReqUserList();
	return true;
}

bool WhisperCommand::Execute(ChatClient& client)
{
	client.ReqWhisper(m_target, m_message);
	return true;
}

bool ChatCommand::Execute(ChatClient& client)
{
	client.ReqChat(m_message);
	return true;
}

bool ExitCommand::Execute(ChatClient& /*client*/)
{
	return false; // 입력 루프 종료
}

bool ErrorCommand::Execute(ChatClient& /*client*/)
{
	std::cout << m_message << std::endl; // 사용자에게 보여주는 UI 출력
	return true;
}

bool EmptyCommand::Execute(ChatClient& /*client*/)
{
	return true;
}
