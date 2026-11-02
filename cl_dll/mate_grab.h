// The framebuffer grab for m8's eyes. Kept out of the SDK headers: windows.h and GL/gl.h
// both want definitions the client SDK also declares (HSPRITE and friends).
#pragma once

void MateGrab_Write(const char *raw_path, const char *tmp_path, int width, int height);
