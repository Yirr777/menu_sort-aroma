#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Scans sd:/wiiu/apps/ (matching the "Homebrew On Wii U Menu" plugin's own
     * discovery rules closely enough) and, for each .wuhb/.rpx found, records
     * the same hash of its SD-relative path that plugin uses as a synthetic
     * title id's lower 32 bits - letting us resolve a name for the homebrew
     * icons it injects into the Wii U Menu, which don't have a real meta.xml
     * we could otherwise read. Safe to call even if the WUHBUtils module
     * isn't loaded (lookups then just always miss). */
    void homebrewNamesScan(void);
    void homebrewNamesFree(void);

    const char *homebrewNamesLookup(uint32_t lowerTitleId);

#ifdef __cplusplus
}
#endif
