#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    void screenInit(void);
    void screenShutdown(void);
    void screenClear(void);
    void screenPrint(const char *fmt, ...);
    int screenGetPrintLine(void);
    void screenSetPrintLine(int line);
    void screenPrintAt(int x, int y, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
