// The client half of m8's eyes: the server asks with a MateView message, and the next
// rendered frame is written to the mod folder for the server to forward.
#pragma once

void MateEyes_Init(void);
void MateEyes_Frame(void);
