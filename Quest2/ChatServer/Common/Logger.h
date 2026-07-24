// Logger.h
#pragma once
#include <mutex>
#include <string>
#include <string_view>
#include <sstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <cstdint>

enum class ELogLevel : uint8_t
{
	Debug,
	Info,
	Warning,
	Error,
};

// 프로세스 전역에서 하나만 존재하는 로거.
// 콘솔 출력을 한 곳으로 모아 mutex로 직렬화하므로, 여러 스레드가 동시에 찍어도
// 줄이 섞이지 않는다. (기존 std::cout 난발의 가장 큰 문제였음)
class Logger
{
public:
	static Logger& Instance()
	{
		static Logger instance;
		return instance;
	}

	void SetMinLevel(ELogLevel level) { m_minLevel = level; }
	bool ShouldLog(ELogLevel level) const { return level >= m_minLevel; }

	void Write(ELogLevel level, std::string_view tag, std::string_view message)
	{
		if (!ShouldLog(level))
		{
			return;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		std::ostream& os = (level >= ELogLevel::Warning) ? std::cerr : std::cout;
		os << '[' << Timestamp() << "][" << ToString(level) << "][" << tag << "] "
			<< message << '\n';
	}

private:
	Logger() = default;
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	static const char* ToString(ELogLevel level)
	{
		switch (level)
		{
		case ELogLevel::Debug:   return "DEBUG";
		case ELogLevel::Info:    return "INFO ";
		case ELogLevel::Warning: return "WARN ";
		case ELogLevel::Error:   return "ERROR";
		default:                 return "?????";
		}
	}

	static std::string Timestamp()
	{
		using namespace std::chrono;
		const auto now = system_clock::now();
		const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
		const std::time_t t = system_clock::to_time_t(now);

		std::tm tm{};
		localtime_s(&tm, &t);

		char buf[32];
		std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
			tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
		return buf;
	}

	std::mutex m_mutex;
	ELogLevel m_minLevel = ELogLevel::Debug;
};

// 소멸 시점에 누적한 문자열을 Logger로 한 번에 넘기는 RAII 스트림.
// 덕분에 기존 `std::cout << a << b;` 스타일을 `LOG_INFO("Tag") << a << b;`로
// 거의 그대로 옮길 수 있고, 한 줄이 통째로 mutex 안에서 출력된다.
class LogStream
{
public:
	LogStream(ELogLevel level, std::string_view tag)
		: m_level(level), m_tag(tag) {}

	~LogStream()
	{
		Logger::Instance().Write(m_level, m_tag, m_oss.str());
	}

	std::ostringstream& Stream() { return m_oss; }

private:
	ELogLevel m_level;
	std::string_view m_tag;
	std::ostringstream m_oss;
};

#define LOG_DEBUG(tag) LogStream(ELogLevel::Debug, tag).Stream()
#define LOG_INFO(tag)  LogStream(ELogLevel::Info, tag).Stream()
#define LOG_WARN(tag)  LogStream(ELogLevel::Warning, tag).Stream()
#define LOG_ERROR(tag) LogStream(ELogLevel::Error, tag).Stream()
