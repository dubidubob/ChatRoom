// ServerInternalTypes.h
// Server Side만 아는 거
#pragma once
#include <winsock2.h>
#include <string>
#include <functional>
#include "Protocol.h"

using RoomDeleteCallback = std::function<void(UINT /* RoomID */)>;

struct ServerToManagerPacket
{
	SOCKET sock = INVALID_SOCKET;
	EPacketType type = EPacketType::None;

	std::shared_ptr<IBody> body = nullptr;

	ServerToManagerPacket() = default;
	ServerToManagerPacket(EPacketType inType) : type(inType) {}
	ServerToManagerPacket(SOCKET inSock, EPacketType inType) : sock(inSock), type(inType) {}
};

struct ManagerToRoomPacket
{
	std::string username = "";
	SOCKET sock = INVALID_SOCKET;
	EPacketType type = EPacketType::None;	

	std::shared_ptr<IBody> body = nullptr;

	ManagerToRoomPacket()= default;
	ManagerToRoomPacket(const ServerToManagerPacket& packet, std::string name) 
		: body(packet.body), username(std::move(name)),	sock(packet.sock), type(packet.type) {}
};