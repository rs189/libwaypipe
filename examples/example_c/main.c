/*
 * main.c
 *
 * MIT License
 *
 * Copyright (c) 2025 rs189
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#include <stdio.h>
#include <unistd.h>
#include "waypipe.h"

int main() {
	Waypipe *wp = waypipe_init();
	if (!wp) { 
		fprintf(stderr, "Waypipe init failed\n"); 

		return 1; 
	}

	int rc = waypipe_start(wp, 15000, 1);
	if (rc != 0) {
		fprintf(stderr, "Waypipe start timed out or failed (rc=%d). Is xdg-desktop-portal running and did you approve the dialogue?\n", rc);
		waypipe_exit(wp);

		return 2;
	}	

	// Poll until a frame is available
	while (true) {
	    uint8_t *rgba = NULL; int w=0,h=0,stride=0;
	    if (waypipe_get_frame(wp, &rgba, &w, &h, &stride) == 0) {
	        printf("Got frame %dx%d stride=%d (first 4 bytes: %u %u %u %u)\n", w, h, stride, rgba[0], rgba[1], rgba[2], rgba[3]);
	        waypipe_free(rgba);
	        break;
	    }
	    sleep(1);
	}	

	waypipe_exit(wp);

	return 0;
}