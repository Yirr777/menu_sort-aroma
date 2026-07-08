#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Extracts the text content of the first top-level <elementName> found in
     * an in-memory XML buffer (not necessarily NUL-terminated). Handles the
     * handful of escaped entities Cafe OS system XML files use. Writes an
     * empty string to out if the element isn't found. */
    void xmlGetElementText(const char *buf, size_t bufSize, const char *elementName,
                            char *out, size_t outSize);

#ifdef __cplusplus
}
#endif
