/* getline() is public domain -- Will Hartung 4/9/09 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "utils/file.h"

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    char *bufptr = NULL;
    char *p = NULL;
    size_t size;
    int c;

    if (lineptr == NULL || stream == NULL || n == NULL)
        return -1;

    bufptr = *lineptr;
    size = *n;

    c = fgetc(stream);
    if (c == EOF)
        return -1;

    if (bufptr == NULL)
    {
        bufptr = malloc(128);
        if (bufptr == NULL)
            return -1;
        size = 128;
    }
    p = bufptr;
    while (c != EOF)
    {
        if ((size_t)(p - bufptr) > (size - 1))
        {
            size = size + 128;
            bufptr = realloc(bufptr, size);
            if (bufptr == NULL)
                return -1;
            p = bufptr + size - 128;
        }
        *p++ = c;
        if (c == '\n')
            break;
        c = fgetc(stream);
    }

    *p++ = '\0';
    *lineptr = bufptr;
    *n = size;

    return p - bufptr - 1;
}

int fcopy(const char *from, const char *to)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd_to < 0)
        goto out_error;

    while ((nread = read(fd_from, buf, sizeof buf)) > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do
        {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);
        return 0;
    }

out_error:
    saved_errno = errno;
    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);
    errno = saved_errno;
    return -1;
}
