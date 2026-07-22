#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <wuhb_utils/utils.h>
#include "utils/homebrewnames.h"

#define APPS_ROOT "fs:/vol/external01/wiiu/apps"
#define MAX_HOMEBREW_ENTRIES 128
#define MAX_NAME_LEN 64

typedef struct
{
    uint32_t hash;
    char name[MAX_NAME_LEN];
} HomebrewEntry;

static HomebrewEntry entries[MAX_HOMEBREW_ENTRIES];
static int entryCount = 0;
static int wuhbUtilsReady = 0;

/* Matches homebrew_on_menu_plugin's hash_string(): unsigned 32-bit,
 * h = 37*h + byte, over the path relative to the SD card root (e.g.
 * "wiiu/apps/menu_sort_aroma/menu_sort_aroma.wuhb"), no modulo applied. */
static uint32_t hashPath(const char *s)
{
    uint32_t h = 0;
    for (; *s; s++)
        h = 37u * h + (unsigned char)*s;
    return h;
}

static int shouldSkipFilename(const char *filename)
{
    if (strcasecmp(filename, "temp.rpx") == 0)
        return 1;
    if (strcasecmp(filename, "temp.wuhb") == 0)
        return 1;
    if (strcasecmp(filename, "temp2.wuhb") == 0)
        return 1;
    if (filename[0] == '.' || filename[0] == '_')
        return 1;
    return 0;
}

/* Mounts a .wuhb and reads its meta/meta.ini for the "longname" key under
 * the "[menu]" section - the same field the plugin itself displays. A tiny
 * line-based parser is enough for this well-defined, wuhbtool-generated
 * format; not a general-purpose INI parser. */
static int tryReadWuhbLongname(const char *fullPath, char *outName, size_t outSize)
{
    static int mountCounter = 0;
    char mountName[16];
    snprintf(mountName, sizeof(mountName), "hbn%d", mountCounter++);

    int32_t mountRes = -1;
    if (WUHBUtils_MountBundle(mountName, fullPath, BundleSource_FileDescriptor, &mountRes) != WUHB_UTILS_RESULT_SUCCESS || mountRes < 0)
        return 0;

    char metaPath[64];
    snprintf(metaPath, sizeof(metaPath), "%s:/meta/meta.ini", mountName);

    int found = 0;
    uint8_t *buf = NULL;
    uint32_t size = 0;
    if (WUHBUtils_ReadWholeFile(metaPath, &buf, &size) == WUHB_UTILS_RESULT_SUCCESS && buf && size > 0)
    {
        char *text = malloc((size_t)size + 1);
        if (text)
        {
            memcpy(text, buf, size);
            text[size] = 0;

            int inMenuSection = 0;
            char *saveptr = NULL;
            char *line = strtok_r(text, "\r\n", &saveptr);
            while (line)
            {
                while (*line == ' ' || *line == '\t')
                    line++;
                if (line[0] == '[')
                {
                    inMenuSection = (strncasecmp(line, "[menu]", 6) == 0);
                }
                else if (inMenuSection && strncasecmp(line, "longname", 8) == 0)
                {
                    char *eq = strchr(line, '=');
                    if (eq)
                    {
                        eq++;
                        while (*eq == ' ' || *eq == '\t')
                            eq++;
                        snprintf(outName, outSize, "%s", eq);
                        found = 1;
                        break;
                    }
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }
            free(text);
        }
    }
    if (buf)
        free(buf);
    WUHBUtils_UnmountBundle(mountName, NULL);
    return found;
}

static void addEntry(const char *fullPath, const char *relPath, const char *filename, int isWuhb)
{
    if (entryCount >= MAX_HOMEBREW_ENTRIES)
        return;

    char name[MAX_NAME_LEN];
    snprintf(name, sizeof(name), "%s", filename);

    if (isWuhb && wuhbUtilsReady)
    {
        char longname[MAX_NAME_LEN];
        if (tryReadWuhbLongname(fullPath, longname, sizeof(longname)))
            snprintf(name, sizeof(name), "%s", longname);
    }

    entries[entryCount].hash = hashPath(relPath);
    snprintf(entries[entryCount].name, sizeof(entries[entryCount].name), "%s", name);
    entryCount++;
}

/* allowSubdirs limits recursion to one level, matching the plugin's own
 * DirList(..., CheckSubfolders, 1) depth. */
static void scanDir(const char *dirPath, const char *relBase, int allowSubdirs)
{
    DIR *d = opendir(dirPath);
    if (!d)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, ent->d_name);

        char relPath[600];
        if (relBase[0])
            snprintf(relPath, sizeof(relPath), "wiiu/apps/%s/%s", relBase, ent->d_name);
        else
            snprintf(relPath, sizeof(relPath), "wiiu/apps/%s", ent->d_name);

        struct stat st;
        if (stat(fullPath, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            if (allowSubdirs)
                scanDir(fullPath, ent->d_name, 0);
            continue;
        }

        if (entryCount >= MAX_HOMEBREW_ENTRIES)
            continue;

        const char *dot = strrchr(ent->d_name, '.');
        if (!dot)
            continue;
        int isWuhb = strcasecmp(dot, ".wuhb") == 0;
        int isRpx = strcasecmp(dot, ".rpx") == 0;
        if (!isWuhb && !isRpx)
            continue;

        if (shouldSkipFilename(ent->d_name))
            continue;

        addEntry(fullPath, relPath, ent->d_name, isWuhb);
    }
    closedir(d);
}

void homebrewNamesScan(void)
{
    entryCount = 0;
    wuhbUtilsReady = (WUHBUtils_InitLibrary() == WUHB_UTILS_RESULT_SUCCESS);
    scanDir(APPS_ROOT, "", 1);
}

void homebrewNamesFree(void)
{
    if (wuhbUtilsReady)
        WUHBUtils_DeInitLibrary();
    wuhbUtilsReady = 0;
    entryCount = 0;
}

const char *homebrewNamesLookup(uint32_t lowerTitleId)
{
    for (int i = 0; i < entryCount; i++)
        if (entries[i].hash == lowerTitleId)
            return entries[i].name;
    return NULL;
}
