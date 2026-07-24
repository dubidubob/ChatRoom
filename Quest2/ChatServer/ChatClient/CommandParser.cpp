// CommandParser.cpp
#include "CommandParser.h"

#include <charconv> // from_chars
#include <string_view>

#include "Protocol.h" // MAX_ROOM_TITLE_LEN 등

namespace
{
	std::unique_ptr<IClientCommand> ParseCreate(const std::string& input, EState state)
	{
		if (state != EState::InLobby)
		{
			return std::make_unique<ErrorCommand>("[오류] 방 안에서는 만들 수 없습니다. 나와서 만들어주세요. (예: /quit)");
		}

		std::string title = input.substr(8); // "/create " 이후
		if (title.empty())
		{
			return std::make_unique<ErrorCommand>("[오류] 방 제목을 입력해주세요. (예: /create MyRoom)");
		}
		if (title.length() >= MAX_ROOM_TITLE_LEN)
		{
			return std::make_unique<ErrorCommand>("[오류] 방 제목이 너무 깁니다. " + std::to_string(MAX_ROOM_TITLE_LEN - 1) + "자 미만으로 입력!");
		}

		return std::make_unique<CreateRoomCommand>(std::move(title));
	}

	std::unique_ptr<IClientCommand> ParseJoin(const std::string& input, EState state)
	{
		if (state != EState::InLobby)
		{
			return std::make_unique<ErrorCommand>("[오류] 이미 방에 있습니다. 나와서 들어가주세요. (예: /quit)");
		}

		std::string roomStr = input.substr(6); // "/join " 이후
		UINT roomID = 0;
		auto [ptr, ec] = std::from_chars(roomStr.data(), roomStr.data() + roomStr.size(), roomID);
		if (ec != std::errc() || ptr != roomStr.data() + roomStr.size())
		{
			return std::make_unique<ErrorCommand>("[오류] 유효한 방 번호를 입력해주세요. (숫자만 가능)");
		}

		return std::make_unique<JoinRoomCommand>(roomID);
	}

	std::unique_ptr<IClientCommand> ParseWhisper(const std::string& input)
	{
		std::string_view view(input);

		constexpr size_t targetStart = 9; // "/whisper "
		size_t messageStart = view.find(' ', targetStart);
		if (messageStart == std::string_view::npos)
		{
			return std::make_unique<ErrorCommand>("[오류] 사용법: /whisper [대상] [메시지] (예: /whisper User1 안녕)");
		}

		std::string_view target = view.substr(targetStart, messageStart - targetStart);
		std::string_view message = view.substr(messageStart + 1);
		if (target.empty() || message.empty())
		{
			return std::make_unique<ErrorCommand>("[오류] 대상 이름이나 메시지가 비어있습니다.");
		}

		return std::make_unique<WhisperCommand>(std::string(target), std::string(message));
	}
}

std::unique_ptr<IClientCommand> CommandParser::Parse(const std::string& input, EState state)
{
	if (input.empty())
	{
		return std::make_unique<EmptyCommand>();
	}

	// 명령어가 아니면 일반 채팅
	if (input.rfind('/', 0) != 0)
	{
		if (state == EState::InRoom)
		{
			return std::make_unique<ChatCommand>(input);
		}
		return std::make_unique<ErrorCommand>("먼저 방에 참여해야 채팅을 보낼 수 있습니다. (예: /join [방ID])");
	}

	if (input.rfind("/create ", 0) == 0)
	{
		return ParseCreate(input, state);
	}
	if (input.rfind("/join ", 0) == 0)
	{
		return ParseJoin(input, state);
	}
	if (input == "/quit")
	{
		// 로비에서는 프로그램 종료, 방 안에서는 방 나가기
		if (state == EState::InLobby)
		{
			return std::make_unique<ExitCommand>();
		}
		return std::make_unique<LeaveRoomCommand>();
	}
	if (input == "/roomlist")
	{
		return std::make_unique<RoomListCommand>();
	}
	if (input == "/userlist")
	{
		return std::make_unique<UserListCommand>();
	}
	if (input.rfind("/whisper ", 0) == 0)
	{
		return ParseWhisper(input);
	}

	return std::make_unique<ErrorCommand>("[오류] 알 수 없는 명령어입니다.");
}
