#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "utils/string.h"

static int startsWith(const char *a, const char *b)
{
    return strncasecmp(b, a, strlen(b)) == 0;
}

char *prepend(char *s, const char *t)
{
    size_t s_len = strlen(s);
    size_t t_len = strlen(t);
    size_t st_len = s_len + t_len;

    s = realloc(s, st_len + 1);
    memmove(s + t_len, s, s_len + 1);
    memcpy(s, t, t_len);

    return s;
}

void removeThe(char *src)
{
    if (startsWith(src, "the "))
    {
        memmove(src, src + 4, strlen(src) - 4);
    }
}
