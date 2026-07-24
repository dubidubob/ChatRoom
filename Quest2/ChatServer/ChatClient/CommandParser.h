// CommandParser.h
#pragma once
#include <memory>
#include <string>

#include "ClientInternalTypes.h" // EState
#include "ClientCommand.h"

// 사용자 입력 문자열 + 현재 상태 → 하나의 커맨드로 변환한다.
// Run() 에 흩어져 있던 파싱/유효성 검사 로직을 이 한 곳에 모은다.
class CommandParser
{
public:
	static std::unique_ptr<IClientCommand> Parse(const std::string& input, EState state);
};
