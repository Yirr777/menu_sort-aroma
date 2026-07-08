#include <stdarg.h>
#include <stdio.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include "utils/screen.h"

/* Delegates to libwhb's on-screen console instead of driving OSScreen
 * directly: it allocates the TV/DRC framebuffers from the MEM1 frame heap
 * and registers PROCUI_CALLBACK_ACQUIRE/RELEASE handlers that free them at
 * exactly the point ProcUI expects, which a hand-rolled memalign()'d buffer
 * doesn't do - without that, the next foreground app (e.g. the Wii U Menu)
 * can be left waiting on a GamePad framebuffer we never properly released,
 * showing a black DRC screen. */

void screenInit(void)
{
    WHBLogConsoleInit();
}

void screenShutdown(void)
{
    WHBLogConsoleFree();
}

void screenPrint(const char *fmt, ...)
{
    char buf[128];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    WHBLogWrite(buf);
    WHBLogConsoleDraw();
}
