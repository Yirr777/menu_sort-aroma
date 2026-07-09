#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Loads a "titleId|PlatformName" file (see vc_platforms.psv) into memory.
     * titleId is the lower 32 bits of the title id, as 8 lowercase hex
     * digits - matching the same "id" our sort logic already reads out of
     * BaristaAccountSaveFile.dat. Safe to call even if the file is missing
     * (lookups then just always miss). */
    void vcPlatformLoad(const char *path);
    void vcPlatformFree(void);

    /* Returns a static platform name (e.g. "NES", "N64") for a given lower-32
     * title id, or NULL if it's not a known Virtual Console title. */
    const char *vcPlatformLookup(uint32_t id);

#ifdef __cplusplus
}
#endif
