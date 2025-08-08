/*
 * Program.cs
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

using System;
using System.Runtime.InteropServices;

class WaypipeExample
{
	[DllImport("libwaypipe.so")] static extern IntPtr waypipe_init();
	[DllImport("libwaypipe.so")] static extern int waypipe_start(IntPtr ctx, uint timeout_ms, int persistent);
	[DllImport("libwaypipe.so")] static extern int waypipe_get_frame(IntPtr ctx, out IntPtr buf, out int w, out int h, out int stride);
	[DllImport("libwaypipe.so")] static extern IntPtr waypipe_get_restore_token(IntPtr ctx);
	[DllImport("libwaypipe.so")] static extern void waypipe_free(IntPtr ptr);
	[DllImport("libwaypipe.so")] static extern void waypipe_exit(IntPtr ctx);
	
	static void Main()
	{
		IntPtr ctx = waypipe_init();
		if (ctx == IntPtr.Zero)
		{
			Console.Error.WriteLine("Waypipe init failed\n"); 
			return;
		}

		int rc = waypipe_start(ctx, 0, 1);
		if (rc != 0)
		{
			Console.Error.WriteLine($"Waypipe start timed out or failed (rc={rc}). Is xdg-desktop-portal running and did you approve the dialogue?\n");
			waypipe_exit(ctx);
			return;
		}

		// Poll until a frame is available
		IntPtr buf = IntPtr.Zero;
		int w = 0, h = 0, stride = 0;
		while (true)
		{
			if (waypipe_get_frame(ctx, out buf, out w, out h, out stride) == 0)
			{
				int size = stride * h;
				byte[] managed = new byte[size];
				Marshal.Copy(buf, managed, 0, size);
				Console.WriteLine($"Got frame {w}x{h} stride={stride} (first 4 bytes: {managed[0]} {managed[1]} {managed[2]} {managed[3]})");
				waypipe_free(buf);
				break;
			}
			System.Threading.Thread.Sleep(1000);
		}

		waypipe_exit(ctx);
	}
}