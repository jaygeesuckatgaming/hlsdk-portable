// The mate's link to m8 (see mate.h).
//
// Frames: [u8 kind][u32 big-endian length][payload]. Kind 1 is a JSON object; the others
// (the mate's voice, dialogue the game played, a view) are m8's to send, and are dropped
// here until the voice and the eyes are wired up.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <deque>
#include <mutex>
#include <string>

#include "extdll.h"
#include "util.h"

#include "mate.h"

#pragma comment(lib, "ws2_32.lib")

static const unsigned short MATE_PORT = 47800;
static const unsigned char MATE_KIND_JSON = 1;
static const size_t MATE_MAX_PENDING = 1u << 20;

static std::mutex g_mate_lock;
static std::deque<std::string> g_mate_outbox;
static std::deque<std::string> g_mate_inbox;
static volatile LONG g_mate_running = 1;
static HANDLE g_mate_thread = NULL;

static void Mate_PushOut(const std::string &json)
{
	std::lock_guard<std::mutex> lock(g_mate_lock);

	if (g_mate_outbox.size() < 256)
		g_mate_outbox.push_back(json);
}

static bool Mate_SendAll(SOCKET socket, const char *data, size_t size)
{
	while (size > 0)
	{
		int sent = send(socket, data, (int)size, 0);

		if (sent <= 0)
			return false;
		data += sent;
		size -= (size_t)sent;
	}
	return true;
}

static bool Mate_SendFrame(SOCKET socket, unsigned char kind, const char *data, size_t size)
{
	unsigned char header[5];
	unsigned int length = htonl((unsigned int)size);

	header[0] = kind;
	memcpy(header + 1, &length, 4);
	return Mate_SendAll(socket, (const char *)header, sizeof(header)) &&
		Mate_SendAll(socket, data, size);
}

/* what came back: whole frames, their JSON queued for the game thread */
static void Mate_Parse(std::string &pending)
{
	while (pending.size() >= 5)
	{
		unsigned char kind = (unsigned char)pending[0];
		unsigned int length;
		size_t frame;

		memcpy(&length, pending.data() + 1, 4);
		length = ntohl(length);
		frame = 5 + (size_t)length;
		if (pending.size() < frame)
			break;
		if (kind == MATE_KIND_JSON && length > 0 && g_mate_inbox.size() < 64)
		{
			std::lock_guard<std::mutex> lock(g_mate_lock);

			g_mate_inbox.push_back(pending.substr(5, length));
		}
		pending.erase(0, frame);
	}
}

static SOCKET Mate_Connect(void)
{
	SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address;
	int no_delay = 1;

	if (socket == INVALID_SOCKET)
		return INVALID_SOCKET;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(MATE_PORT);
	inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

	if (connect(socket, (sockaddr *)&address, sizeof(address)) != 0)
	{
		closesocket(socket);
		return INVALID_SOCKET;
	}
	setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&no_delay, sizeof(no_delay));
	return socket;
}

static DWORD WINAPI Mate_Thread(LPVOID)
{
	WSADATA wsa;
	std::string pending;
	SOCKET socket = INVALID_SOCKET;

	WSAStartup(MAKEWORD(2, 2), &wsa);

	while (InterlockedCompareExchange(&g_mate_running, 1, 1) != 0)
	{
		std::deque<std::string> outbox;
		bool alive = true;

		if (socket == INVALID_SOCKET)
		{
			socket = Mate_Connect();
			if (socket == INVALID_SOCKET)
			{
				Sleep(2000);
				continue;
			}
			OutputDebugStringA("[mate] connected to m8\n");
		}

		{
			std::lock_guard<std::mutex> lock(g_mate_lock);

			outbox.swap(g_mate_outbox);
		}
		for (std::deque<std::string>::iterator json = outbox.begin(); json != outbox.end(); ++json)
		{
			if (!Mate_SendFrame(socket, MATE_KIND_JSON, json->data(), json->size()))
			{
				alive = false;
				break;
			}
		}
		if (!alive)
		{
			closesocket(socket);
			socket = INVALID_SOCKET;
			pending.clear();
			continue;
		}

		{
			fd_set readable;
			timeval wait = { 0, 20000 };

			FD_ZERO(&readable);
			FD_SET(socket, &readable);
			if (select(0, &readable, NULL, NULL, &wait) > 0)
			{
				char buffer[4096];
				int got = recv(socket, buffer, sizeof(buffer), 0);

				if (got <= 0)
				{
					closesocket(socket);
					socket = INVALID_SOCKET;
					pending.clear();
					continue;
				}
				pending.append(buffer, (size_t)got);
				if (pending.size() > MATE_MAX_PENDING)
					pending.clear();
				else
					Mate_Parse(pending);
			}
		}
	}

	if (socket != INVALID_SOCKET)
		closesocket(socket);
	WSACleanup();
	return 0;
}

void Mate_Init(void)
{
	if (g_mate_thread)
		return;

	g_mate_running = 1;
	g_mate_thread = CreateThread(NULL, 0, Mate_Thread, NULL, 0, NULL);
}

void Mate_SendJson(const char *json)
{
	if (json && json[0])
		Mate_PushOut(json);
}

/* once a server frame: what came back is printed (until the voice and the actions use it),
   and what m8 should know is sent at most twice a second */
void Mate_Frame(void)
{
	std::deque<std::string> inbox;
	const char *map;
	static double next_state = 0.0;
	static char last_map[64] = "";

	if (!gpGlobals)
		return;

	{
		std::lock_guard<std::mutex> lock(g_mate_lock);

		inbox.swap(g_mate_inbox);
	}
	for (std::deque<std::string>::iterator line = inbox.begin(); line != inbox.end(); ++line)
		ALERT(at_console, "[mate] %s\n", line->c_str());

	map = STRING(gpGlobals->mapname);
	if (!map || !map[0])
		return;
	if (gpGlobals->time < next_state)
		return;
	next_state = gpGlobals->time + 0.5;

	{
		char state[512];

		snprintf(state, sizeof(state),
			"{\"type\":\"state\",\"game\":\"Half-Life\",\"player_name\":\"Gordon\","
			"\"level\":\"%s\",\"state\":\"standing in %s\"}", map, map);
		Mate_SendJson(state);
	}

	if (strcmp(last_map, map) != 0)
	{
		char event[256];

		snprintf(last_map, sizeof(last_map), "%s", map);
		snprintf(event, sizeof(event),
			"{\"type\":\"event\",\"text\":\"Half-Life reached %s.\",\"react\":true}", map);
		Mate_SendJson(event);
	}
}
