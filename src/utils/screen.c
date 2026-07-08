#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <malloc.h>
#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include "utils/screen.h"

static int current_line = 0;
static void *tvBuffer = NULL;
static void *drcBuffer = NULL;
static uint32_t tvBufferSize = 0;
static uint32_t drcBufferSize = 0;

static void screenFlip(void)
{
    DCFlushRange(tvBuffer, tvBufferSize);
    DCFlushRange(drcBuffer, drcBufferSize);
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

static void screenFill(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t color = (r << 24) | (g << 16) | (b << 8) | a;
    OSScreenClearBufferEx(SCREEN_TV, color);
    OSScreenClearBufferEx(SCREEN_DRC, color);
}

void screenClear(void)
{
    screenFill(0, 0, 0, 0);
    screenFlip();
    screenFill(0, 0, 0, 0);
    screenFlip();
}

void screenInit(void)
{
    OSScreenInit();
    tvBufferSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    drcBufferSize = OSScreenGetBufferSizeEx(SCREEN_DRC);

    tvBuffer = memalign(0x100, tvBufferSize);
    drcBuffer = memalign(0x100, drcBufferSize);

    OSScreenSetBufferEx(SCREEN_TV, tvBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuffer);

    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);
}

void screenShutdown(void)
{
    OSScreenShutdown();
    if (tvBuffer)
        free(tvBuffer);
    if (drcBuffer)
        free(drcBuffer);
    tvBuffer = NULL;
    drcBuffer = NULL;
}

void screenPrint(const char *fmt, ...)
{
    if (current_line == 16)
    {
        screenClear();
        current_line = 0;
    }
    char *tmp = NULL;
    va_list va;
    va_start(va, fmt);
    if ((vasprintf(&tmp, fmt, va) >= 0) && tmp)
    {
        OSScreenPutFontEx(SCREEN_TV, 0, current_line, tmp);
        OSScreenPutFontEx(SCREEN_DRC, 0, current_line, tmp);
        screenFlip();
        OSScreenPutFontEx(SCREEN_TV, 0, current_line, tmp);
        OSScreenPutFontEx(SCREEN_DRC, 0, current_line, tmp);
        screenFlip();
    }
    va_end(va);
    if (tmp)
        free(tmp);
    current_line++;
}

int screenGetPrintLine(void)
{
    return current_line - 1;
}

void screenSetPrintLine(int line)
{
    current_line = line;
}

void screenPrintAt(int x, int y, const char *fmt, ...)
{
    char *tmp = NULL;
    va_list va;
    va_start(va, fmt);
    if ((vasprintf(&tmp, fmt, va) >= 0) && tmp)
    {
        OSScreenPutFontEx(SCREEN_TV, x, y, tmp);
        OSScreenPutFontEx(SCREEN_DRC, x, y, tmp);
        screenFlip();
        OSScreenPutFontEx(SCREEN_TV, x, y, tmp);
        OSScreenPutFontEx(SCREEN_DRC, x, y, tmp);
        screenFlip();
    }
    va_end(va);
    if (tmp)
        free(tmp);
}
