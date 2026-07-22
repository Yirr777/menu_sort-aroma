#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <coreinit/dynload.h>
#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <mocha/mocha.h>

#include "act_wrapper.h"
#include "utils/screen.h"
#include "utils/dict.h"
#include "utils/string.h"
#include "utils/file.h"
#include "utils/xmltext.h"
#include "utils/homebrewnames.h"

#define TITLE_TEXT "Menu Sort Aroma v1.3.0 - Yardape8000 & doino-gretchenliev"
#define CREDIT_TEXT "Ported and maintained by Yirr777"
#define HBL_TITLE_ID 0x13374842
#define MAX_ITEMS_COUNT 300

static const char *cafeXmlPath = "storage_slc:/proc/prefs/cafe.xml";
static const char *syshaxXmlPath = "storage_slc:/config/syshax.xml";
static const char *systemXmlPath = "storage_slc:/config/system.xml";
static const char *dontmovePath = "fs:/vol/external01/wiiu/apps/menu_sort_aroma/dontmove";
static const char *gamemapPath = "fs:/vol/external01/wiiu/apps/menu_sort_aroma/titlesmap";
static const char *backupPath = "fs:/vol/external01/wiiu/apps/menu_sort_aroma/BaristaAccountSaveFile.dat";
static const char *homebrewExcludePath = "fs:/vol/external01/wiiu/apps/menu_sort_aroma/dontmove_homebrew.txt";
static const char *languages[] = {"JA", "EN", "FR", "DE", "IT", "ES", "ZHS", "KO", "NL", "PT", "RU", "ZHT"};
static char languageText[14] = "longname_en";

static int badNamingMode = 0;
static int ignoreThe = 0;
static int backup = 0;
static int restore = 0;
static int count = 0;

struct MenuItemStruct
{
    uint32_t ID;
    uint32_t type;
    uint32_t titleIDPrefix;
    char name[65];
};

enum itemTypes
{
    MENU_ITEM_NAND = 0x01,
    MENU_ITEM_USB = 0x02,
    MENU_ITEM_DISC = 0x05,
    MENU_ITEM_VWII = 0x09,
    MENU_ITEM_FLDR = 0x10
};

static int smartStrcmp(const char *a, const char *b, const uint32_t a_id, const uint32_t b_id)
{
    char *ac = malloc(strlen(a) + 1);
    char *bc = malloc(strlen(b) + 1);

    strcpy(ac, a);
    strcpy(bc, b);

    if (ignoreThe)
    {
        removeThe(ac);
        removeThe(bc);
    }

    if (badNamingMode)
    {
        int a_id_size = (int)((ceil(log10(a_id)) + 1) * sizeof(char));
        char a_id_string[a_id_size];

        int b_id_size = (int)((ceil(log10(b_id)) + 1) * sizeof(char));
        char b_id_string[b_id_size];

        itoa(a_id, a_id_string, 16);
        itoa(b_id, b_id_string, 16);

        struct nlist *np;
        np = lookup(a_id_string);
        if (np != NULL)
        {
            ac = prepend(ac, np->defn);
        }

        np = lookup(b_id_string);
        if (np != NULL)
        {
            bc = prepend(bc, np->defn);
        }
    }

    int result = strcasecmp(ac, bc);
    free(ac);
    free(bc);
    return result;
}

static int fSortCond(const void *c1, const void *c2)
{
    return smartStrcmp(((struct MenuItemStruct *)c1)->name,
                        ((struct MenuItemStruct *)c2)->name,
                        ((struct MenuItemStruct *)c1)->ID,
                        ((struct MenuItemStruct *)c2)->ID);
}

static int getXMLelementInt(const char *buff, size_t buffSize, const char *elementName, int base)
{
    char text[40] = "";
    xmlGetElementText(buff, buffSize, elementName, text, sizeof(text));
    return (int)strtol(text, NULL, base);
}

static int readToBuffer(char **ptr, size_t *bufferSize, const char *path)
{
    FILE *fp;
    size_t size;
    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    rewind(fp);
    *ptr = malloc(size);
    memset(*ptr, 0, size);
    fread(*ptr, 1, size, fp);
    fclose(fp);
    *bufferSize = size;
    return 0;
}

#define UPPER_TITLE_ID_HOMEBREW 0x0005000fu

/* Homebrew icons injected by the "Homebrew On Wii U Menu" plugin don't have
 * a fixed, guessable title id the way dontmove.txt entries do (their id is a
 * hash of wherever the user happens to have installed them), so utility
 * apps people generally want to keep put are excluded here by resolved name
 * instead - one per line in dontmove_homebrew.txt, matched as a
 * case-insensitive substring to tolerate minor naming differences between
 * versions/forks. Editable by the user, same as dontmove.txt/titlesmap.psv;
 * no rebuild required to add or remove an entry. */
static char **homebrewExcludeNames = NULL;
static int homebrewExcludeCount = 0;

static void loadHomebrewExcludeList(const char *path)
{
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

    homebrewExcludeNames = malloc(sizeof(char *) * lines);
    if (!homebrewExcludeNames)
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

        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ' || line[l - 1] == '\t'))
            line[--l] = 0;

        if (l > 0)
        {
            homebrewExcludeNames[homebrewExcludeCount] = malloc(l + 1);
            if (homebrewExcludeNames[homebrewExcludeCount])
            {
                memcpy(homebrewExcludeNames[homebrewExcludeCount], line, l + 1);
                homebrewExcludeCount++;
            }
        }
        free(line);
    }

    fclose(fp);
}

static void freeHomebrewExcludeList(void)
{
    for (int i = 0; i < homebrewExcludeCount; i++)
        free(homebrewExcludeNames[i]);
    free(homebrewExcludeNames);
    homebrewExcludeNames = NULL;
    homebrewExcludeCount = 0;
}

static int isDefaultExcludedHomebrewName(const char *name)
{
    size_t nameLen = strlen(name);
    for (int i = 0; i < homebrewExcludeCount; i++)
    {
        size_t needleLen = strlen(homebrewExcludeNames[i]);
        if (needleLen == 0 || needleLen > nameLen)
            continue;
        for (size_t pos = 0; pos + needleLen <= nameLen; pos++)
        {
            if (strncasecmp(name + pos, homebrewExcludeNames[i], needleLen) == 0)
                return 1;
        }
    }
    return 0;
}

static void getIDname(uint32_t id, uint32_t titleIDPrefix, char *name, size_t nameSize, uint32_t type)
{
    name[0] = 0;

    if (titleIDPrefix == UPPER_TITLE_ID_HOMEBREW)
    {
        /* A synthetic id the "Homebrew On Wii U Menu" plugin injects for a
         * homebrew app on the SD card - there's no real meta.xml to read
         * for these, so resolve the name via the same sd:/wiiu/apps/ scan
         * and path hash the plugin itself uses. */
        const char *hn = homebrewNamesLookup(id);
        if (hn)
            strncpy(name, hn, nameSize - 1);
        return;
    }

    char *xBuffer = NULL;
    size_t xSize = 0;
    char path[255] = "";
    sprintf(path, "storage_%s:/usr/title/%08x/%08x/meta/meta.xml", (type == MENU_ITEM_USB) ? "usb" : "mlc", titleIDPrefix, id);
    if (readToBuffer(&xBuffer, &xSize, path) == 0 && xBuffer != NULL)
    {
        xmlGetElementText(xBuffer, xSize, languageText, name, nameSize);
        free(xBuffer);
    }
}

/* _SYSGetSystemApplicationTitleId isn't wrapped by wut; resolve it the same
 * way the system itself does, via a sysapp.rpl export lookup. */
typedef uint64_t (*SYSGetSystemApplicationTitleIdFn)(int32_t);

static uint64_t sysGetSystemApplicationTitleId(int32_t index)
{
    OSDynLoad_Module module;
    if (OSDynLoad_Acquire("sysapp.rpl", &module) != OS_DYNLOAD_OK)
        return 0;

    SYSGetSystemApplicationTitleIdFn fn = NULL;
    OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "_SYSGetSystemApplicationTitleId", (void **)&fn);
    if (!fn)
        return 0;

    return fn(index);
}

/* The "Homebrew On Wii U Menu" plugin (widely used with Aroma) redirects the
 * Wii U Menu's own read of BaristaAccountSaveFile.dat to a copy it keeps on
 * the SD card, so homebrew stays visible even without the plugin active -
 * but per its own docs it only ever *creates* that copy once and never
 * refreshes it, so changes we make to the real save file are invisible to
 * the Wii U Menu while that plugin is loaded. Mirror our write there too
 * (only if that cache already exists - if it doesn't, the plugin hasn't
 * been used yet and will correctly pick up our already-updated data itself).
 * Path/logic mirrors https://github.com/wiiu-env/homebrew_on_menu_plugin
 * (src/SaveRedirection.cpp, src/utils/utils.cpp GetSerialId). */
static int getConsoleSerialId(char *out, size_t outSize)
{
    out[0] = 0;
    int handle = MCP_Open();
    if (handle < 0)
        return 0;

    int ok = 0;
    /* The MCP IPC command needs a 0x40-aligned buffer - without this,
     * MCP_GetSysProdSettings fails to fill in settings at all. */
    MCPSysProdSettings settings __attribute__((aligned(0x40)));
    memset(&settings, 0, sizeof(settings));
    if (MCP_GetSysProdSettings(handle, &settings) == 0)
    {
        char codeId[sizeof(settings.code_id) + 1];
        char serialId[sizeof(settings.serial_id) + 1];
        memcpy(codeId, settings.code_id, sizeof(settings.code_id));
        codeId[sizeof(settings.code_id)] = 0;
        memcpy(serialId, settings.serial_id, sizeof(settings.serial_id));
        serialId[sizeof(settings.serial_id)] = 0;
        snprintf(out, outSize, "%s%s", codeId, serialId);
        ok = 1;
    }
    MCP_Close(handle);
    return ok;
}

/* The "Homebrew On Wii U Menu" plugin redirects the Wii U Menu's *entire*
 * save I/O (both reads and writes) to this SD-card copy while it's active -
 * every folder/icon edit made through the Wii U Menu itself lands here, not
 * in the real save on MLC at all. If we sort the real file instead, we're
 * working from stale data and, worse, deleting the plugin's cache afterward
 * (the previous approach) makes it re-copy from that stale real file next
 * boot - discarding whatever the user organized through the menu since.
 * Whenever this cache exists, it - not the real MLC path - is the correct
 * file to read, sort, and write back to. */
static int getHomebrewOnMenuPluginCachePath(char *out, size_t outSize, uint32_t userPersistentId)
{
    char serialId[32];
    if (!getConsoleSerialId(serialId, sizeof(serialId)) || serialId[0] == 0)
        return 0;

    snprintf(out, outSize,
             "fs:/vol/external01/wiiu/homebrew_on_menu_plugin/%s/save/%08x/BaristaAccountSaveFile.dat",
             serialId, 0x80000000u | userPersistentId);

    FILE *fp = fopen(out, "rb");
    if (!fp)
        return 0; // Plugin cache doesn't exist (or plugin isn't used).
    fclose(fp);
    return 1;
}

int main(void)
{
    WHBProcInit();

    int fsMounted = 0;
    int failed = 1;
    char failError[65] = "";
    char *fBuffer = NULL;
    size_t fSize = 0;
    int usb_Connected = 0;
    uint32_t sysmenuId = 0;
    uint32_t cbhcID = 0;
    uint32_t userPersistentId = 0;

    screenInit();
    screenPrint("------------------------------------------------------------");
    screenPrint(TITLE_TEXT);
    screenPrint(CREDIT_TEXT);
    screenPrint("------------------------------------------------------------");
    screenPrint("Choose sorting method:");
    screenPrint("  B  - standard sorting");
    screenPrint("  A  - standard sorting (ignoring leading 'The')");
    screenPrint("  X  - bad naming mode sorting");
    screenPrint("  Y  - bad naming mode sorting (ignoring leading 'The')");
    screenPrint("  +  - backup the current order (incl. folders)");
    screenPrint("  -  - restore the current order (incl. folders)");
    screenPrint("  L  - count items only (no changes)");

    VPADStatus vpad;
    VPADReadError vpadError;

    char modeText[25] = "";
    char ignoreTheText[25] = "";

    while (WHBProcIsRunning())
    {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
        uint32_t pressedBtns = 0;

        if (vpadError == VPAD_READ_SUCCESS)
            pressedBtns = vpad.trigger | vpad.hold;

        if (pressedBtns & VPAD_BUTTON_B)
        {
            badNamingMode = 0;
            ignoreThe = 0;
            strcpy(modeText, "standard sorting");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_A)
        {
            badNamingMode = 0;
            ignoreThe = 1;
            strcpy(modeText, "standard sorting");
            strcpy(ignoreTheText, "(ignoring leading 'The')");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_X)
        {
            badNamingMode = 1;
            ignoreThe = 0;
            strcpy(modeText, "bad naming mode sorting");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_Y)
        {
            badNamingMode = 1;
            ignoreThe = 1;
            strcpy(modeText, "bad naming mode sorting");
            strcpy(ignoreTheText, "(ignoring leading 'The')");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_PLUS)
        {
            backup = 1;
            strcpy(modeText, "backup");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_MINUS)
        {
            restore = 1;
            strcpy(modeText, "restore");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_L)
        {
            count = 1;
            strcpy(modeText, "count");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_HOME)
            goto prgEnd;

        OSSleepTicks(OSMillisecondsToTicks(1));
    }

    char modeSelectedText[50] = "";
    sprintf(modeSelectedText, "Starting %s%s...", modeText, ignoreTheText);
    screenPrint(modeSelectedText);

    // Get Wii U Menu Title. Do this before mounting the file system, it
    // screws it up if done after.
    {
        uint64_t sysmenuIdUll = sysGetSystemApplicationTitleId(0);
        if ((sysmenuIdUll & 0xffffffff00000000ull) == 0x0005001000000000ull)
            sysmenuId = (uint32_t)(sysmenuIdUll & 0x00000000ffffffffull);
        else
        {
            strcpy(failError, "Failed to get Wii U Menu Title!");
            goto prgEnd;
        }
    }

    // Get current user account slot
    actInitialize();
    uint8_t userSlot = actGetSlotNo();
    userPersistentId = actGetPersistentIdEx(userSlot);
    actFinalize();

    // Initialize FS
    if (Mocha_InitLibrary() != MOCHA_RESULT_SUCCESS)
    {
        strcpy(failError, "Mocha_InitLibrary failed\n");
        goto prgEnd;
    }
    fsMounted = 1;

    if (Mocha_MountFS("storage_slc", NULL, "/vol/system") != MOCHA_RESULT_SUCCESS)
    {
        strcpy(failError, "Failed to mount SLC!");
        goto prgEnd;
    }
    if (Mocha_MountFS("storage_mlc", NULL, "/vol/storage_mlc01") != MOCHA_RESULT_SUCCESS)
    {
        strcpy(failError, "Failed to mount MLC!");
        goto prgEnd;
    }
    usb_Connected = Mocha_MountFS("storage_usb", NULL, "/vol/storage_usb01") == MOCHA_RESULT_SUCCESS;

    // Get Country Code. Really should use SCIGetCafeLanguage(), but until
    // then, just read cafe.xml. This only affects which meta.xml field is
    // used to display a title's name while sorting, so a failure here isn't
    // fatal - backup/restore/count don't need it at all, and sorting just
    // falls back to the "longname_en" default set at startup.
    if (readToBuffer(&fBuffer, &fSize, cafeXmlPath) == 0 && fBuffer != NULL)
    {
        int language = getXMLelementInt(fBuffer, fSize, "language", 10);
        if (language < 0 || language >= (int)(sizeof(languages) / sizeof(languages[0])))
            language = 1; // EN
        sprintf(languageText, "longname_%s", languages[language]);
        free(fBuffer);
        fBuffer = NULL;
    }

    // Get CBHC Title. If syshax.xml exists, then assume cbhc exists. Also
    // not fatal - worst case the CBHC title just isn't excluded from sorting.
    {
        FILE *fp = fopen(syshaxXmlPath, "rb");
        if (fp)
        {
            fclose(fp);
            if (readToBuffer(&fBuffer, &fSize, systemXmlPath) == 0 && fBuffer != NULL)
            {
                cbhcID = (uint32_t)getXMLelementInt(fBuffer, fSize, "default_title_id", 10);
                free(fBuffer);
                fBuffer = NULL;
            }
        }
    }

    // Read Don't Move titles. First try dontmoveX.txt where X is the user
    // ID, then fall back to dontmove.txt.
    uint32_t *dmItem = NULL;
    int dmTotal = 0;
    {
        char dmPath[96] = "";
        sprintf(dmPath, "%s%1x.txt", dontmovePath, userPersistentId & 0x0000000f);
        FILE *fp = fopen(dmPath, "rb");
        if (!fp)
        {
            sprintf(dmPath, "%s.txt", dontmovePath);
            fp = fopen(dmPath, "rb");
        }
        if (fp)
        {
            int ch, lines = 0;
            do
            {
                ch = fgetc(fp);
                if (ch == '\n')
                    lines++;
            } while (ch != EOF);
            rewind(fp);

            dmTotal = lines;
            dmItem = (uint32_t *)malloc(sizeof(uint32_t) * lines);

            for (int i = 0; i < lines; i++)
            {
                char *currTitleID = NULL;
                size_t len = 0;
                if (getline(&currTitleID, &len, fp) >= 0)
                    dmItem[i] = (uint32_t)strtol(currTitleID, NULL, 16);
                else
                    dmItem[i] = 0;
                free(currTitleID);
            }

            fclose(fp);
        }
    }

    if (badNamingMode)
    {
        char gmPath[96] = "";
        sprintf(gmPath, "%s%1x.psv", gamemapPath, userPersistentId & 0x0000000f);
        FILE *fp = fopen(gmPath, "rb");
        if (!fp)
        {
            sprintf(gmPath, "%s.psv", gamemapPath);
            fp = fopen(gmPath, "rb");
        }
        if (fp)
        {
            int ch, lines = 0;
            do
            {
                ch = fgetc(fp);
                if (ch == '\n')
                    lines++;
            } while (ch != EOF);
            rewind(fp);

            for (int i = 0; i < lines; i++)
            {
                char *currTitleID = NULL;
                size_t len = 0;
                if (getline(&currTitleID, &len, fp) < 0)
                {
                    free(currTitleID);
                    break;
                }

                char *id = strtok(currTitleID, "|");
                char *group = strtok(NULL, "|");
                if (id && group)
                {
                    group = strtok(group, "\n");
                    install(id, group);
                }
                free(currTitleID);
            }

            fclose(fp);
        }
    }

    // Read Menu Data
    struct MenuItemStruct menuItem[MAX_ITEMS_COUNT];
    bool folderExists[61] = {false};
    bool moveableItem[MAX_ITEMS_COUNT];
    /* True only for slots that actually held a sortable title, as opposed to
     * slots that are movable but simply empty (never assigned a title).
     * Rewriting only wasCollected slots - instead of every movable slot -
     * keeps the occupied/empty pattern around fixed items (folders,
     * dontmove.txt entries, etc.) intact, so they don't visually drift to a
     * different grid position as a side effect of empty gaps elsewhere
     * getting compacted away. */
    bool wasCollected[MAX_ITEMS_COUNT];
    folderExists[0] = true;
    char mlcBaristaPath[255] = "";
    sprintf(mlcBaristaPath, "storage_mlc:/usr/save/00050010/%08x/user/%08x/BaristaAccountSaveFile.dat", sysmenuId, (unsigned int)userPersistentId);

    char baristaPath[255] = "";
    int usingPluginCache = getHomebrewOnMenuPluginCachePath(baristaPath, sizeof(baristaPath), userPersistentId);
    if (!usingPluginCache)
        strcpy(baristaPath, mlcBaristaPath);
    else
        screenPrint("Using Homebrew On Menu plugin's save copy.");

    int itemsCount = 0;

    if (backup)
    {
        fcopy(baristaPath, backupPath);
    }
    else if (restore)
    {
        fcopy(backupPath, baristaPath);
        if (usingPluginCache)
            fcopy(baristaPath, mlcBaristaPath);
    }
    else
    {
        if (readToBuffer(&fBuffer, &fSize, baristaPath) < 0)
        {
            strcpy(failError, "Could not open BaristaAccountSaveFile.dat\n");
            goto prgEnd;
        }
        if (fBuffer == NULL)
        {
            strcpy(failError, "Memory not allocated for BaristaAccountSaveFile.dat\n");
            goto prgEnd;
        }

        if (!count)
        {
            // Always back up the current (pristine, not-yet-modified) order
            // before an actual sort touches it, regardless of whether the
            // user also explicitly asked for one via '+'.
            fcopy(baristaPath, backupPath);
            screenPrint("Backed up current order first.");
        }

        homebrewNamesScan();
        loadHomebrewExcludeList(homebrewExcludePath);

        // Main Menu - First pass - Get names. Only movable items are added.
        for (int fNum = 0; fNum <= 60; fNum++)
        {
            if (!folderExists[fNum])
                continue;
            int currItemNum = 0;
            int movableItemsCount = 0;
            int maxItemsCount = MAX_ITEMS_COUNT;
            int folderOffset = 0;
            if (fNum != 0)
            {
                maxItemsCount = 60;
                folderOffset = 0x002D24 + ((fNum - 1) * (60 * 16 * 2 + 56));
            }
            int usbOffset = maxItemsCount * 16;
            int diagNonEmpty = 0;
            int diagVwii = 0;
            for (int i = 0; i < maxItemsCount; i++)
            {
                moveableItem[i] = true;
                wasCollected[i] = false;
                int itemOffset = i * 16 + folderOffset;
                uint32_t id = 0;
                uint32_t type = 0;
                memcpy(&id, fBuffer + itemOffset + 4, sizeof(uint32_t));
                memcpy(&type, fBuffer + itemOffset + 8, sizeof(uint32_t));

                if (count && fNum != 0 && !(id == 0 && type == 0))
                    diagNonEmpty++;

                if ((id == HBL_TITLE_ID)
                    || (cbhcID && (id == cbhcID))
                    || (type == MENU_ITEM_DISC)
                    || (type == MENU_ITEM_VWII))
                {
                    if (count && fNum != 0 && type == MENU_ITEM_VWII)
                        diagVwii++;
                    moveableItem[i] = false;
                    itemsCount++;
                    continue;
                }
                if ((fNum == 0) && (type == MENU_ITEM_FLDR))
                {
                    if ((id > 0) && (id <= 60))
                        folderExists[id] = true;
                    if (count)
                        screenPrint("Folder icon: mainSlot=%d id=%u %s", i, id,
                                    (id > 0 && id <= 60) ? "(recognized)" : "(OUT OF RANGE)");
                    moveableItem[i] = false;
                    continue;
                }
                if (type == MENU_ITEM_NAND)
                {
                    uint32_t idH = 0;
                    memcpy(&idH, fBuffer + itemOffset, sizeof(uint32_t));

                    if ((idH != 0x00050000) && (idH != 0x00050002) && (idH != 0) && (idH != UPPER_TITLE_ID_HOMEBREW))
                    {
                        moveableItem[i] = false;
                        continue;
                    }

                    if (id == 0)
                    {
                        if (!usb_Connected)
                            continue;
                        itemOffset += usbOffset;
                        id = 0;
                        memcpy(&id, fBuffer + itemOffset + 4, sizeof(uint32_t));
                        type = (uint8_t)fBuffer[itemOffset + 0x0b];
                        if ((id == 0) || (type != MENU_ITEM_USB))
                            continue;
                    }

                    itemsCount++;

                    for (int j = 0; j < dmTotal; j++)
                    {
                        if (id == dmItem[j])
                        {
                            moveableItem[i] = false;
                            break;
                        }
                    }

                    if (!moveableItem[i])
                        continue;

                    memcpy(&menuItem[currItemNum].titleIDPrefix, fBuffer + itemOffset, sizeof(uint32_t));
                    getIDname(id, menuItem[currItemNum].titleIDPrefix, menuItem[currItemNum].name, 65, type);
                    if (menuItem[currItemNum].name[0] == 0)
                    {
                        /* No readable meta.xml (e.g. a Homebrew Launcher
                         * channel/forwarder installed under its own title id,
                         * which varies by install method and isn't covered by
                         * HBL_TITLE_ID) - leave it in place rather than
                         * sorting an unnamed entry to one end of the list. */
                        moveableItem[i] = false;
                        continue;
                    }
                    if (menuItem[currItemNum].titleIDPrefix == UPPER_TITLE_ID_HOMEBREW
                        && isDefaultExcludedHomebrewName(menuItem[currItemNum].name))
                    {
                        moveableItem[i] = false;
                        continue;
                    }
                    menuItem[currItemNum].ID = id;
                    menuItem[currItemNum].type = type;
                    wasCollected[i] = true;
                    currItemNum++;
                }
            }
            movableItemsCount = currItemNum;

            if (count && fNum != 0)
                screenPrint("Folder %d: %d slot(s) used, %d vWii, %d named/sortable",
                            fNum, diagNonEmpty, diagVwii, movableItemsCount);

            if (!count)
            {
                qsort(menuItem, movableItemsCount, sizeof(struct MenuItemStruct), fSortCond);

                /* Compact within each contiguous run of movable slots (a
                 * "segment" bounded by fixed items - folders, dontmove.txt
                 * entries, etc. - or the array edges), instead of either
                 * compacting globally (aroma24 and earlier: hides gaps, but
                 * can shift a segment's fixed neighbor to a different visual
                 * position) or not compacting at all (aroma25/26: keeps
                 * every fixed item's position exact, but leaves whatever
                 * gaps already existed scattered where they were). Consuming
                 * the globally-sorted list in segment order left-to-right
                 * still yields correct overall alphabetical order, since
                 * each segment gets exactly the next slice of it. */
                currItemNum = 0;
                int i = 0;
                while (i < maxItemsCount)
                {
                    if (!moveableItem[i])
                    {
                        i++;
                        continue;
                    }

                    int segStart = i;
                    int segEnd = i;
                    while (segEnd < maxItemsCount && moveableItem[segEnd])
                        segEnd++;

                    int segRealCount = 0;
                    for (int k = segStart; k < segEnd; k++)
                        if (wasCollected[k])
                            segRealCount++;

                    for (int k = segStart; k < segEnd; k++)
                    {
                        int itemOffset = k * 16 + folderOffset;
                        uint32_t idNAND = 0;
                        uint32_t idNANDh = 0;
                        uint32_t idUSB = 0;
                        uint32_t idUSBh = 0;
                        if (k - segStart < segRealCount)
                        {
                            if (menuItem[currItemNum].type == MENU_ITEM_NAND)
                            {
                                idNAND = menuItem[currItemNum].ID;
                                idNANDh = menuItem[currItemNum].titleIDPrefix;
                            }
                            else
                            {
                                idUSB = menuItem[currItemNum].ID;
                                idUSBh = menuItem[currItemNum].titleIDPrefix;
                            }
                            currItemNum++;
                        }

                        memcpy(fBuffer + itemOffset, &idNANDh, sizeof(uint32_t));
                        memcpy(fBuffer + itemOffset + 4, &idNAND, sizeof(uint32_t));
                        memset(fBuffer + itemOffset + 8, 0, 8);
                        fBuffer[itemOffset + 0x0b] = 1;

                        int usbItemOffset = itemOffset + usbOffset;

                        memcpy(fBuffer + usbItemOffset, &idUSBh, sizeof(uint32_t));
                        memcpy(fBuffer + usbItemOffset + 4, &idUSB, sizeof(uint32_t));
                        memset(fBuffer + usbItemOffset + 8, 0, 8);
                        fBuffer[usbItemOffset + 0x0b] = 2;
                    }

                    i = segEnd;
                }
            }
        }

        if (!count)
        {
            FILE *fp = fopen(baristaPath, "wb");
            if (fp)
            {
                fwrite(fBuffer, 1, fSize, fp);
                fclose(fp);
                if (usingPluginCache)
                    fcopy(baristaPath, mlcBaristaPath);
            }
            else
            {
                strcpy(failError, "Could not write to BaristaAccountSaveFile.dat\n");
                goto prgEnd;
            }
        }

        free(fBuffer);
        fBuffer = NULL;
        free(dmItem);
        dmItem = NULL;
        homebrewNamesFree();
        freeHomebrewExcludeList();
    }

    screenPrint("done.");

    {
        char text[20] = "";
        sprintf(text, "User ID: %1x", (unsigned int)(userPersistentId & 0x0000000f));
        screenPrint(text);
    }
    if (itemsCount != 0)
    {
        char countText[32] = "";
        sprintf(countText, "Items count: %d/%d", itemsCount, MAX_ITEMS_COUNT);
        screenPrint(countText);
    }
    failed = 0;

prgEnd:
    if (failed && failError[0])
        screenPrint(failError);

    if (fBuffer)
        free(fBuffer);

    if (fsMounted)
    {
        Mocha_UnmountFS("storage_slc");
        Mocha_UnmountFS("storage_mlc");
        Mocha_UnmountFS("storage_usb");
        Mocha_DeInitLibrary();
    }

    /* Press HOME to bring up the system overlay and pick "Home Menu" to
     * exit, same as every other homebrew app - WHBProcIsRunning() reports
     * false once that's done. Forcing the transition ourselves via
     * SYSLaunchMenu() got us out reliably too, but consistently left the
     * GamePad screen black afterwards; letting the OS-driven overlay flow
     * (which every other app already relies on) run its course avoids that. */
    screenPrint("Press HOME to return to the Wii U Menu.");
    while (WHBProcIsRunning())
        OSSleepTicks(OSMillisecondsToTicks(20));

    WHBProcShutdown();

    return 0;
}
