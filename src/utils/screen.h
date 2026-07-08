#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    void screenInit(void);
    void screenShutdown(void);
    void screenPrint(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
