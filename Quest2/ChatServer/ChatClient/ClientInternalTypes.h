//ClientInternalTypes.h
#pragma once
#include "protocol.h"

struct ClientPacket
{
	EPacketType type = EPacketType::None;
	std::shared_ptr<IBody> body = nullptr;
};

// 클라이언트의 입력 처리 상태. 로그인 전 단계 + 로그인 후 위치(로비/방).
// CommandParser 가 상태에 따라 같은 입력을 다른 커맨드로 해석하므로 외부에서도 필요.
enum class EState : uint8_t
{
	NewLoggingIn,
	OldLoggingIn,
	WaitingLogIn,

	InLobby,
	InRoom
};