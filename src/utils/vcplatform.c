#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils/vcplatform.h"
#include "utils/file.h"

typedef struct
{
    uint32_t id;
    char platform[16];
} VcPlatformEntry;

static VcPlatformEntry *entries = NULL;
static int entryCount = 0;

void vcPlatformLoad(const char *path)
{
    vcPlatformFree();

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return;

    int lines = 0;
    int ch;
    do
    {
        ch = fgetc(fp);
        if (ch == '\n')
            lines++;
    } while (ch != EOF);
    rewind(fp);

    if (lines <= 0)
    {
        fclose(fp);
        return;
    }

    entries = (VcPlatformEntry *)malloc(sizeof(VcPlatformEntry) * lines);
    if (!entries)
    {
        fclose(fp);
        return;
    }

    for (int i = 0; i < lines; i++)
    {
        char *line = NULL;
        size_t len = 0;
        if (getline(&line, &len, fp) < 0)
        {
            free(line);
            break;
        }

        char *id = strtok(line, "|");
        char *platform = strtok(NULL, "\n");
        if (id && platform)
        {
            entries[entryCount].id = (uint32_t)strtoul(id, NULL, 16);
            strncpy(entries[entryCount].platform, platform, sizeof(entries[entryCount].platform) - 1);
            entries[entryCount].platform[sizeof(entries[entryCount].platform) - 1] = 0;
            entryCount++;
        }
        free(line);
    }

    fclose(fp);
}

void vcPlatformFree(void)
{
    free(entries);
    entries = NULL;
    entryCount = 0;
}

const char *vcPlatformLookup(uint32_t id)
{
    for (int i = 0; i < entryCount; i++)
    {
        if (entries[i].id == id)
            return entries[i].platform;
    }
    return NULL;
}
