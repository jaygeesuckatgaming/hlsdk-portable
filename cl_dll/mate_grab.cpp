// Reads the rendered frame and writes it for the server (see mate_grab.h).

#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "mate_grab.h"

void MateGrab_Write(const char *raw_path, const char *tmp_path, int width, int height)
{
	int target_width;
	int target_height;
	int x;
	int y;
	std::vector<unsigned char> source;
	std::vector<unsigned char> target;
	FILE *file;

	if (!raw_path || !tmp_path || width <= 0 || height <= 0 || width > 4096 || height > 4096)
		return;
	target_width = 960;
	if (target_width > width)
		target_width = width;
	target_height = height * target_width / width;
	if (target_height <= 0)
		return;

	source.resize((size_t)width * height * 3);
	target.resize((size_t)target_width * target_height * 3);
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, source.data());

	/* (GL rows are bottom-up: write the target from the bottom of the source) */
	for (y = 0; y < target_height; y++)
	{
		int source_y = height - 1 - (y * height / target_height);

		for (x = 0; x < target_width; x++)
		{
			int source_x = x * width / target_width;

			memcpy(&target[((size_t)y * target_width + x) * 3],
				&source[((size_t)source_y * width + source_x) * 3], 3);
		}
	}

	file = fopen(tmp_path, "wb");
	if (!file)
		return;
	fwrite(&target_width, 4, 1, file);
	fwrite(&target_height, 4, 1, file);
	fwrite(target.data(), 1, target.size(), file);
	fclose(file);
	remove(raw_path);
	rename(tmp_path, raw_path);
}
