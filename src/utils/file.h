#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    ssize_t getline(char **lineptr, size_t *n, FILE *stream);
    int fcopy(const char *from, const char *to);

#ifdef __cplusplus
}
#endif
