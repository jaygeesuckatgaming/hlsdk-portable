// The mate's link to m8 (see mate.h).
//
// Frames: [u8 kind][u32 big-endian length][payload]. Kind 1 is a JSON object; the others
// (the mate's voice, dialogue the game played, a view) are m8's to send, and are dropped
// here until the voice and the eyes are wired up.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "extdll.h"
#include "util.h"

#include "mate.h"

#pragma comment(lib, "ws2_32.lib")

static const unsigned short MATE_PORT = 47800;
static const unsigned char MATE_KIND_JSON = 1;
static const unsigned char MATE_KIND_AUDIO = 2;
static const size_t MATE_MAX_PENDING = 1u << 20;
static const size_t MATE_MAX_AUDIO = 6u << 20;

static std::mutex g_mate_lock;
static std::deque<std::string> g_mate_outbox;
static std::deque<std::string> g_mate_inbox;
static volatile LONG g_mate_running = 1;
static HANDLE g_mate_thread = NULL;

static unsigned int g_msg_say = 0;
static unsigned int g_msg_stop = 0;
static unsigned int g_audio_rate = 24000;
static std::string g_audio;
static bool g_audio_done = false;
static bool g_audio_stop = false;

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
		if (kind == MATE_KIND_JSON && length > 0)
		{
			std::string json = pending.substr(5, length);
			const char *rate = strstr(json.c_str(), "\"sample_rate\":");

			std::lock_guard<std::mutex> lock(g_mate_lock);

			if (json.find("\"audio_end\"") != std::string::npos)
				g_audio_done = true;
			else if (json.find("\"stop_audio\"") != std::string::npos)
			{
				g_audio_stop = true;
				g_audio.clear();
			}
			if (rate)
				g_audio_rate = (unsigned int)atoi(rate + 14);
			if (g_mate_inbox.size() < 64)
				g_mate_inbox.push_back(json);
		}
		else if (kind == MATE_KIND_AUDIO && length > 0)
		{
			std::lock_guard<std::mutex> lock(g_mate_lock);

			if (g_audio.size() + length <= MATE_MAX_AUDIO)
				g_audio.append(pending.data() + 5, length);
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
	g_msg_say = REG_USER_MSG("MateSay", -1);
	g_msg_stop = REG_USER_MSG("MateStop", 1);
	g_mate_thread = CreateThread(NULL, 0, Mate_Thread, NULL, 0, NULL);
}

void Mate_SendJson(const char *json)
{
	if (json && json[0])
		Mate_PushOut(json);
}

/* m8's voice line, as a WAV in the mod's sound folder, and its name for the client */
static void Mate_WriteWav(const std::string &pcm, unsigned int rate, char *out, size_t out_size)
{
	char dir[512];
	char path[600];
	static unsigned int counter = 0;
	static std::deque<std::string> files;
	FILE *file;
	unsigned int data_size = (unsigned int)pcm.size();
	unsigned int riff_size = 36 + data_size;
	unsigned int byte_rate = rate * 2;
	unsigned int fmt_size = 16;
	unsigned short format = 1, channels = 1, block = 2, bits = 16;

	out[0] = 0;
	GET_GAME_DIR(dir);
	std::string sound = std::string(dir) + "\\sound";

	CreateDirectoryA(sound.c_str(), NULL);
	counter++;
	char name[64];

	snprintf(name, sizeof(name), "mate_%u.wav", counter);
	snprintf(path, sizeof(path), "%s\\%s", sound.c_str(), name);

	file = fopen(path, "wb");
	if (!file)
	{
		ALERT(at_console, "[mate] could not write %s\n", path);
		return;
	}
	fwrite("RIFF", 1, 4, file);
	fwrite(&riff_size, 4, 1, file);
	fwrite("WAVE", 1, 4, file);
	fwrite("fmt ", 1, 4, file);
	fwrite(&fmt_size, 4, 1, file);
	fwrite(&format, 2, 1, file);
	fwrite(&channels, 2, 1, file);
	fwrite(&rate, 4, 1, file);
	fwrite(&byte_rate, 4, 1, file);
	fwrite(&block, 2, 1, file);
	fwrite(&bits, 2, 1, file);
	fwrite("data", 1, 4, file);
	fwrite(&data_size, 4, 1, file);
	fwrite(pcm.data(), 1, data_size, file);
	fclose(file);

	files.push_back(path);
	while (files.size() > 16)
	{
		remove(files.front().c_str());
		files.pop_front();
	}

	snprintf(out, out_size, "%s", path);
}

/* once a server frame: what came back is printed (until the actions use it), a finished
   voice line goes to the client, and what m8 should know is sent at most twice a second */
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
	{
		ALERT(at_console, "[mate] %s\n", line->c_str());
		Mate_WorldOrder(line->c_str());
	}

	{
		bool stop, done;
		unsigned int rate;

		{
			std::lock_guard<std::mutex> lock(g_mate_lock);

			stop = g_audio_stop;
			g_audio_stop = false;
			done = g_audio_done;
			g_audio_done = false;
			rate = g_audio_rate;
		}
		if (stop && g_msg_stop)
		{
			MESSAGE_BEGIN(MSG_ALL, g_msg_stop, NULL);
			MESSAGE_END();
		}
		if (done && g_msg_say)
		{
			std::string pcm;
			char path[600];

			path[0] = 0;
			{
				std::lock_guard<std::mutex> lock(g_mate_lock);

				pcm.swap(g_audio);
			}
			if (!pcm.empty())
			{
				Mate_WriteWav(pcm, rate ? rate : 24000, path, sizeof(path));
				ALERT(at_console, "[mate] voice file %s\n", path[0] ? path : "(not written)");
				if (path[0])
				{
					MESSAGE_BEGIN(MSG_ALL, g_msg_say, NULL);
					WRITE_STRING(path);
					MESSAGE_END();
				}
			}
		}
	}

	map = STRING(gpGlobals->mapname);
	if (!map || !map[0])
		return;

	if (last_map[0] != 0 && strcmp(last_map, map) != 0)
		Mate_WorldReset();
	Mate_WorldFrame();

	if (gpGlobals->time < next_state)
		return;
	next_state = gpGlobals->time + 0.5;

	{
		char state[4096];

		if (Mate_WorldState(state, sizeof(state)))
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
