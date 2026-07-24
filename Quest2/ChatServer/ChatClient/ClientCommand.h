// ClientCommand.h
#pragma once
#include <string>
#include "Protocol.h" // UINT

class ChatClient;

// 사용자 입력 한 줄을 "해석된 하나의 행동"으로 캡슐화한 커맨드.
// 파싱(CommandParser)과 실행(ChatClient 액션 호출)을 분리해,
// Run() 의 거대한 if-else 파싱 체인을 없앤다.
class IClientCommand
{
public:
	virtual ~IClientCommand() = default;

	// false 를 반환하면 클라이언트 입력 루프를 종료한다.
	virtual bool Execute(ChatClient& client) = 0;
};

class CreateRoomCommand : public IClientCommand
{
public:
	explicit CreateRoomCommand(std::string title) : m_title(std::move(title)) {}
	bool Execute(ChatClient& client) override;

private:
	std::string m_title;
};

class JoinRoomCommand : public IClientCommand
{
public:
	explicit JoinRoomCommand(UINT roomID) : m_roomID(roomID) {}
	bool Execute(ChatClient& client) override;

private:
	UINT m_roomID;
};

class LeaveRoomCommand : public IClientCommand
{
public:
	bool Execute(ChatClient& client) override;
};

class RoomListCommand : public IClientCommand
{
public:
	bool Execute(ChatClient& client) override;
};

class UserListCommand : public IClientCommand
{
public:
	bool Execute(ChatClient& client) override;
};

class WhisperCommand : public IClientCommand
{
public:
	WhisperCommand(std::string target, std::string message)
		: m_target(std::move(target)), m_message(std::move(message)) {}
	bool Execute(ChatClient& client) override;

private:
	std::string m_target;
	std::string m_message;
};

class ChatCommand : public IClientCommand
{
public:
	explicit ChatCommand(std::string message) : m_message(std::move(message)) {}
	bool Execute(ChatClient& client) override;

private:
	std::string m_message;
};

// 로비에서 /quit → 프로그램 종료.
class ExitCommand : public IClientCommand
{
public:
	bool Execute(ChatClient& client) override;
};

// 파싱 실패/유효성 오류. 사용자에게 사유를 출력만 한다.
class ErrorCommand : public IClientCommand
{
public:
	explicit ErrorCommand(std::string message) : m_message(std::move(message)) {}
	bool Execute(ChatClient& client) override;

private:
	std::string m_message;
};

// 빈 입력 등 아무것도 하지 않는 커맨드.
class EmptyCommand : public IClientCommand
{
public:
	bool Execute(ChatClient& client) override;
};
