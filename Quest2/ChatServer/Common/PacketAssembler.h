//PacketAssembler.h
#pragma once
#include <functional>
#include <array>
#include <memory> // shared ptr, 스마트 포인터 용 헤더인데 memory인 이유 : 메모리의 소유권(ownership), 생명주기(lifetime), 할당(allocation)을 다루는 모든 도구의 집합

#include "ServerInternalTypes.h"

template <typename ContextType>
class PacketAssembler
{
public:
    using BodyFactory = std::function<std::shared_ptr<IBody>(const PacketHeader*)>; // Code 영역 가리키는 포인터

    explicit PacketAssembler(BodyFactory factory)
        :m_bodyFactory(std::move(factory)) {};


    void ProcessBytes(const char* buffer, int len, std::vector<ContextType>& outPacketContexts)
    {
        if (m_writePos + len > BUFFER_SIZE) // 버퍼에 len 쓸 수 없을 때,
        {
            int dataSize = m_writePos - m_readPos;
            if (dataSize > 0) // 미처리 데이터만 맨 앞으로 이동
            {
                memmove(m_recvBuffer.data(), m_recvBuffer.data() + m_readPos, dataSize);
            }
            m_readPos = 0;
            m_writePos = dataSize;
        }
        
        memcpy(m_recvBuffer.data() + m_writePos, buffer, len); // 데이터 복사
        m_writePos += len;

        while (true)
        {
            int readableSize = m_writePos - m_readPos;

            if (readableSize < sizeof(PacketHeader))
                break;

            const auto* header = reinterpret_cast<const PacketHeader*>(m_recvBuffer.data() + m_readPos);

            if (header->bodyLength > BUFFER_SIZE - sizeof(PacketHeader))
            {
                m_readPos = m_writePos = 0;
                throw std::runtime_error("PacketAssembler: 이게 가능한가");
            }

            const size_t requiredLength = sizeof(PacketHeader) + header->bodyLength;

            if (readableSize < requiredLength)
                break;

            std::shared_ptr<IBody> createdBody = m_bodyFactory(header);

            if (createdBody)
            {
                auto& context = outPacketContexts.emplace_back();
                context.type = header->type;
                context.body = createdBody;
            }

            m_readPos += requiredLength;
        }
    }

private:
    static constexpr int BUFFER_SIZE = MAX_BUFFER_SIZE * 2; // recv()의 tempbuffer 크기, 잔여 + 새 버퍼 데이터
    std::array<char, BUFFER_SIZE> m_recvBuffer; // TODO : array? vector

    int m_readPos = 0;
    int m_writePos = 0;

    BodyFactory m_bodyFactory; // 주입 받은 생성 전략
};