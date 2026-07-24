// SafeQueue.h
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility>

// 여러 생산자 스레드가 Push 하고, 소비자 스레드 하나가 Pop 으로 블로킹하며 꺼내가는 큐.
// ChatManager / ChatRoom 이 각각 손으로 관리하던 (queue + mutex + condition_variable)
// 3종 세트를 하나로 묶은 것.
//
// Close() 는 우아한 종료용이다. 소비 스레드가 빈 큐에서 대기 중이어도 깨워서
// Pop 이 nullopt 를 반환하게 만들어, 루프를 안전하게 빠져나오게 한다.
template <typename T>
class SafeQueue
{
public:
	void Push(T value)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queue.push(std::move(value));
		}
		m_condition.notify_one();
	}

	// 항목이 생기거나 큐가 닫힐 때까지 블록한다.
	// 닫힌 뒤 큐가 비면 nullopt 를 반환한다 → 소비 루프 종료 신호.
	std::optional<T> Pop()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_condition.wait(lock, [this] { return !m_queue.empty() || m_closed; });

		if (m_queue.empty())
		{
			return std::nullopt;
		}

		T value = std::move(m_queue.front());
		m_queue.pop();
		return value;
	}

	void Close()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_closed = true;
		}
		m_condition.notify_all();
	}

	bool Empty() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.empty();
	}

private:
	mutable std::mutex m_mutex;
	std::condition_variable m_condition;
	std::queue<T> m_queue;
	bool m_closed = false;
};
