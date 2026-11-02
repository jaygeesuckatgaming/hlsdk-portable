// The mate's link to m8: the game side of m8's TCP link (127.0.0.1:47800).
//
// The socket lives on a thread of its own; the game thread only queues what to send and
// drains what came back (Mate_Frame, from StartFrame). Nothing here calls the engine from
// that thread. Mate_World* is the companion itself (mate_world.cpp).
#pragma once

void Mate_Init();
void Mate_SendJson(const char *json);
void Mate_Frame();

void Mate_WorldPrecache();
void Mate_WorldReset();
void Mate_WorldFrame();
void Mate_WorldOrder(const char *json);
int Mate_WorldState(char *out, int size);
