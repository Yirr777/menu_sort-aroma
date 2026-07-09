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
#include "utils/vcplatform.h"

#define TITLE_TEXT "Sort Wii U Menu v1.2.0 (Aroma) - Yardape8000 & doino-gretchenliev"
#define HBL_TITLE_ID 0x13374842
#define MAX_ITEMS_COUNT 300

static const char *cafeXmlPath = "storage_slc:/proc/prefs/cafe.xml";
static const char *syshaxXmlPath = "storage_slc:/config/syshax.xml";
static const char *systemXmlPath = "storage_slc:/config/system.xml";
static const char *dontmovePath = "fs:/vol/external01/wiiu/apps/menu_sort/dontmove";
static const char *gamemapPath = "fs:/vol/external01/wiiu/apps/menu_sort/titlesmap";
static const char *vcPlatformPath = "fs:/vol/external01/wiiu/apps/menu_sort/vc_platforms.psv";
static const char *backupPath = "fs:/vol/external01/wiiu/apps/menu_sort/BaristaAccountSaveFile.dat";
static const char *languages[] = {"JA", "EN", "FR", "DE", "IT", "ES", "ZHS", "KO", "NL", "PT", "RU", "ZHT"};
static char languageText[14] = "longname_en";

static int badNamingMode = 0;
static int ignoreThe = 0;
static int backup = 0;
static int restore = 0;
static int count = 0;
static int organize = 0;

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

/* Folder N's data (see the file format notes at the end of this file) lives
 * at BARISTA_FOLDER_BASE_OFFSET + (N-1)*BARISTA_FOLDER_STRIDE, as a 60-item
 * NAND array, then a 60-item USB array, then a 56-byte info block (name +
 * color). */
#define BARISTA_FOLDER_BASE_OFFSET 0x002D24
#define BARISTA_FOLDER_ITEM_COUNT 60
#define BARISTA_FOLDER_STRIDE (BARISTA_FOLDER_ITEM_COUNT * 16 * 2 + 56)
#define BARISTA_FOLDER_USB_OFFSET (BARISTA_FOLDER_ITEM_COUNT * 16)
#define BARISTA_FOLDER_INFO_OFFSET (BARISTA_FOLDER_ITEM_COUNT * 16 * 2)
#define BARISTA_FOLDER_NAME_CHARS 17
#define BARISTA_FOLDER_COLOR_OFFSET 0x32
#define BARISTA_FOLDER_INFO_SIZE 56

static int folderDataOffsetFor(int folderId)
{
    return BARISTA_FOLDER_BASE_OFFSET + (folderId - 1) * BARISTA_FOLDER_STRIDE;
}

static int folderInfoOffsetFor(int folderId)
{
    return folderDataOffsetFor(folderId) + BARISTA_FOLDER_INFO_OFFSET;
}

/* Folder names are stored as fixed-length UTF-16BE; our platform/console
 * names are plain ASCII, so a byte-level round trip is all we need. */
static void utf16beToAscii(const uint8_t *src, int maxChars, char *out, size_t outSize)
{
    size_t o = 0;
    for (int i = 0; i < maxChars && o < outSize - 1; i++)
    {
        uint16_t ch = ((uint16_t)src[i * 2] << 8) | src[i * 2 + 1];
        if (ch == 0)
            break;
        out[o++] = (ch < 128) ? (char)ch : '?';
    }
    out[o] = 0;
}

static void asciiToUtf16be(const char *in, uint8_t *dst, int maxChars)
{
    int i;
    for (i = 0; in[i] != 0 && i < maxChars; i++)
    {
        dst[i * 2] = 0;
        dst[i * 2 + 1] = (uint8_t)in[i];
    }
    for (; i < maxChars; i++)
    {
        dst[i * 2] = 0;
        dst[i * 2 + 1] = 0;
    }
}

/* An entry's 16 bytes are all zero (title id 0, no type) when the slot is
 * genuinely free - matches how the existing sort/write-back logic already
 * treats id==0 as "nothing here", regardless of a possibly-stale type byte
 * left over from a previous write. */
static int entryIsEmpty(const char *fBuffer, int offset)
{
    uint32_t idH = 0, id = 0;
    memcpy(&idH, fBuffer + offset, sizeof(uint32_t));
    memcpy(&id, fBuffer + offset + 4, sizeof(uint32_t));
    return idH == 0 && id == 0;
}

enum folderColors
{
    FOLDER_COLOR_BLUE = 0,
    FOLDER_COLOR_GREEN = 1,
    FOLDER_COLOR_YELLOW = 2,
    FOLDER_COLOR_ORANGE = 3,
    FOLDER_COLOR_RED = 4,
    FOLDER_COLOR_PINK = 5,
    FOLDER_COLOR_PURPLE = 6,
    FOLDER_COLOR_GREY = 7
};

/* A color loosely themed to each console's own branding, so the folders
 * this mode creates are visually distinct from one another. */
static uint8_t folderColorFor(const char *folderName)
{
    if (strcasecmp(folderName, "Wii") == 0)
        return FOLDER_COLOR_BLUE;
    if (strcasecmp(folderName, "NES") == 0)
        return FOLDER_COLOR_RED;
    if (strcasecmp(folderName, "SNES") == 0)
        return FOLDER_COLOR_PURPLE;
    if (strcasecmp(folderName, "N64") == 0)
        return FOLDER_COLOR_GREEN;
    if (strcasecmp(folderName, "GBA") == 0)
        return FOLDER_COLOR_ORANGE;
    if (strcasecmp(folderName, "DS") == 0)
        return FOLDER_COLOR_PINK;
    if (strcasecmp(folderName, "PC Engine") == 0)
        return FOLDER_COLOR_YELLOW;
    if (strcasecmp(folderName, "MSX") == 0)
        return FOLDER_COLOR_GREY;
    return FOLDER_COLOR_BLUE;
}

/* Finds a folder already named folderName (case-insensitive), or creates a
 * new one (a free folder id 1-60, a free main-menu slot for its icon, and
 * its info block) if none exists. Returns the folder id, or 0 if there's no
 * room left for a new folder. */
static int findOrCreateFolder(char *fBuffer, bool folderExists[61], const char *folderName)
{
    char name[BARISTA_FOLDER_NAME_CHARS + 1];
    for (int fid = 1; fid <= 60; fid++)
    {
        if (!folderExists[fid])
            continue;
        utf16beToAscii((const uint8_t *)(fBuffer + folderInfoOffsetFor(fid)), BARISTA_FOLDER_NAME_CHARS, name, sizeof(name));
        if (strcasecmp(name, folderName) == 0)
            return fid;
    }

    int newFid = 0;
    for (int fid = 1; fid <= 60; fid++)
    {
        if (!folderExists[fid])
        {
            newFid = fid;
            break;
        }
    }
    if (newFid == 0)
        return 0;

    int freeSlot = -1;
    for (int i = 0; i < MAX_ITEMS_COUNT; i++)
    {
        if (entryIsEmpty(fBuffer, i * 16))
        {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0)
        return 0;

    int off = freeSlot * 16;
    memset(fBuffer + off, 0, 16);
    uint32_t fidVal = (uint32_t)newFid;
    memcpy(fBuffer + off + 4, &fidVal, sizeof(uint32_t));
    fBuffer[off + 0x0b] = MENU_ITEM_FLDR;

    int infoOff = folderInfoOffsetFor(newFid);
    memset(fBuffer + infoOff, 0, BARISTA_FOLDER_INFO_SIZE);
    asciiToUtf16be(folderName, (uint8_t *)(fBuffer + infoOff), BARISTA_FOLDER_NAME_CHARS);
    fBuffer[infoOff + BARISTA_FOLDER_COLOR_OFFSET] = (char)folderColorFor(folderName);

    folderExists[newFid] = true;
    return newFid;
}

/* Finds a free slot within folder folderId's NAND or USB array. Returns the
 * absolute byte offset into fBuffer, or -1 if that array is full. */
static int findFreeFolderSlot(const char *fBuffer, int folderId, uint32_t type)
{
    int base = folderDataOffsetFor(folderId) + (type == MENU_ITEM_USB ? BARISTA_FOLDER_USB_OFFSET : 0);
    for (int i = 0; i < BARISTA_FOLDER_ITEM_COUNT; i++)
    {
        int off = base + i * 16;
        if (entryIsEmpty(fBuffer, off))
            return off;
    }
    return -1;
}

/* Moves the entry at [dataSrcOff, dataSrcOff+16) into the given (existing)
 * folder's next free slot of the same storage type, then clears the source
 * slot(s). A USB-backed main-menu item has its actual data in the parallel
 * USB array (dataSrcOff), while its NAND-array slot only holds a zero-id
 * placeholder (nandPlaceholderOff) that must be cleared too; pass -1 for
 * nandPlaceholderOff for a self-contained entry (a plain NAND item, or a
 * vWii title, which isn't split across NAND/USB arrays at all). */
static void moveEntryToFolder(char *fBuffer, int folderId, uint32_t type, int dataSrcOff, int nandPlaceholderOff)
{
    int dstOff = findFreeFolderSlot(fBuffer, folderId, type);
    if (dstOff < 0)
        return; // that folder is full - leave the entry where it is.

    memcpy(fBuffer + dstOff, fBuffer + dataSrcOff, 16);
    memset(fBuffer + dataSrcOff, 0, 16);
    if (nandPlaceholderOff >= 0)
        memset(fBuffer + nandPlaceholderOff, 0, 16);
}

/* Scans the main menu (only - not other folders) for vWii titles and known
 * Virtual Console titles (per vc_platforms.psv) and relocates them into a
 * "Wii"/"NES"/"SNES"/etc. folder, creating it if needed. Runs before the
 * normal per-folder sort pass, so newly created/populated folders get
 * alphabetized along with everything else afterwards. Titles in dontmove.txt,
 * the CBHC title, and the Homebrew Launcher are left alone, same as sorting. */
static void organizeIntoFolders(char *fBuffer, bool folderExists[61], uint32_t *dmItem, int dmTotal, uint32_t cbhcID, int usb_Connected)
{
    for (int i = 0; i < MAX_ITEMS_COUNT; i++)
    {
        int off = i * 16;
        uint32_t id = 0, type = 0;
        memcpy(&id, fBuffer + off + 4, sizeof(uint32_t));
        memcpy(&type, fBuffer + off + 8, sizeof(uint32_t));
        if (type == MENU_ITEM_FLDR && id > 0 && id <= 60)
            folderExists[id] = true;
    }

    for (int i = 0; i < MAX_ITEMS_COUNT; i++)
    {
        int off = i * 16;
        uint32_t idH = 0, id = 0, type = 0;
        memcpy(&idH, fBuffer + off, sizeof(uint32_t));
        memcpy(&id, fBuffer + off + 4, sizeof(uint32_t));
        memcpy(&type, fBuffer + off + 8, sizeof(uint32_t));

        if (type == MENU_ITEM_VWII)
        {
            int folderId = findOrCreateFolder(fBuffer, folderExists, "Wii");
            if (folderId != 0)
                moveEntryToFolder(fBuffer, folderId, MENU_ITEM_VWII, off, -1);
            continue;
        }

        if (type != MENU_ITEM_NAND)
            continue; // folders, disc, empty slots, etc. - not our concern here.

        if ((idH != 0x00050000) && (idH != 0x00050002) && (idH != 0))
            continue; // Settings, Mii Maker, etc.

        int dataOff = off;
        uint32_t realId = id;
        uint32_t realType = MENU_ITEM_NAND;
        int nandPlaceholderOff = -1;

        if (id == 0)
        {
            if (!usb_Connected)
                continue;
            int usbOff = off + MAX_ITEMS_COUNT * 16;
            uint32_t usbId = 0;
            memcpy(&usbId, fBuffer + usbOff + 4, sizeof(uint32_t));
            uint8_t usbType = (uint8_t)fBuffer[usbOff + 0x0b];
            if (usbId == 0 || usbType != MENU_ITEM_USB)
                continue; // genuinely empty slot
            dataOff = usbOff;
            realId = usbId;
            realType = MENU_ITEM_USB;
            nandPlaceholderOff = off;
        }

        if (realId == HBL_TITLE_ID || (cbhcID && realId == cbhcID))
            continue;

        bool dontMove = false;
        for (int j = 0; j < dmTotal; j++)
        {
            if (realId == dmItem[j])
            {
                dontMove = true;
                break;
            }
        }
        if (dontMove)
            continue;

        const char *platformName = vcPlatformLookup(realId);
        if (!platformName)
            continue;

        int folderId = findOrCreateFolder(fBuffer, folderExists, platformName);
        if (folderId == 0)
            continue;

        moveEntryToFolder(fBuffer, folderId, realType, dataOff, nandPlaceholderOff);
    }
}

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

static void getIDname(uint32_t id, uint32_t titleIDPrefix, char *name, size_t nameSize, uint32_t type)
{
    char *xBuffer = NULL;
    size_t xSize = 0;
    char path[255] = "";
    name[0] = 0;
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

static void deleteStaleHomebrewOnMenuPluginCache(uint32_t userPersistentId)
{
    char serialId[32];
    if (!getConsoleSerialId(serialId, sizeof(serialId)) || serialId[0] == 0)
        return;

    char cachePath[192];
    snprintf(cachePath, sizeof(cachePath),
             "fs:/vol/external01/wiiu/homebrew_on_menu_plugin/%s/save/%08x/BaristaAccountSaveFile.dat",
             serialId, 0x80000000u | userPersistentId);

    FILE *fp = fopen(cachePath, "rb");
    if (!fp)
        return; // Plugin cache doesn't exist (or plugin isn't used) - nothing to do.
    fclose(fp);

    /* Delete rather than overwrite: the plugin recreates it by copying from
     * the real save file (which we've already fixed) the next time it
     * initializes, so this is just as correct without depending on our own
     * path derivation staying byte-for-byte identical to a future version
     * of the plugin's - a stale cache we fail to delete is a no-op, not a
     * wrong write. */
    remove(cachePath);
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
    screenPrint("------------------------------------------------------------");
    screenPrint("Choose sorting method:");
    screenPrint("  B  - standard sorting");
    screenPrint("  A  - standard sorting (ignoring leading 'The')");
    screenPrint("  X  - bad naming mode sorting");
    screenPrint("  Y  - bad naming mode sorting (ignoring leading 'The')");
    screenPrint("  +  - backup the current order (incl. folders)");
    screenPrint("  -  - restore the current order (incl. folders)");
    screenPrint("  L  - count items only (no changes)");
    screenPrint("  R  - organize VC/Wii into folders, then standard sort");

    VPADStatus vpad;
    VPADReadError vpadError;

    char modeText[48] = "";
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

        if (pressedBtns & VPAD_BUTTON_R)
        {
            organize = 1;
            strcpy(modeText, "organize into folders + standard sorting");
            break;
        }

        if (pressedBtns & VPAD_BUTTON_HOME)
            goto prgEnd;

        OSSleepTicks(OSMillisecondsToTicks(1));
    }

    char modeSelectedText[96] = "";
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

    if (organize)
        vcPlatformLoad(vcPlatformPath);

    // Read Menu Data
    struct MenuItemStruct menuItem[MAX_ITEMS_COUNT];
    bool folderExists[61] = {false};
    bool moveableItem[MAX_ITEMS_COUNT];
    char baristaPath[255] = "";
    folderExists[0] = true;
    sprintf(baristaPath, "storage_mlc:/usr/save/00050010/%08x/user/%08x/BaristaAccountSaveFile.dat", sysmenuId, (unsigned int)userPersistentId);

    int itemsCount = 0;

    if (backup)
    {
        fcopy(baristaPath, backupPath);
    }
    else if (restore)
    {
        fcopy(backupPath, baristaPath);
        deleteStaleHomebrewOnMenuPluginCache(userPersistentId);
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

        if (organize)
        {
            organizeIntoFolders(fBuffer, folderExists, dmItem, dmTotal, cbhcID, usb_Connected);
            vcPlatformFree();
        }

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
            for (int i = 0; i < maxItemsCount; i++)
            {
                moveableItem[i] = true;
                int itemOffset = i * 16 + folderOffset;
                uint32_t id = 0;
                uint32_t type = 0;
                memcpy(&id, fBuffer + itemOffset + 4, sizeof(uint32_t));
                memcpy(&type, fBuffer + itemOffset + 8, sizeof(uint32_t));

                if ((id == HBL_TITLE_ID)
                    || (cbhcID && (id == cbhcID))
                    || (type == MENU_ITEM_DISC)
                    || (type == MENU_ITEM_VWII))
                {
                    moveableItem[i] = false;
                    itemsCount++;
                    continue;
                }
                if ((fNum == 0) && (type == MENU_ITEM_FLDR))
                {
                    if ((id > 0) && (id <= 60))
                        folderExists[id] = true;
                    moveableItem[i] = false;
                    continue;
                }
                if (type == MENU_ITEM_NAND)
                {
                    uint32_t idH = 0;
                    memcpy(&idH, fBuffer + itemOffset, sizeof(uint32_t));

                    if ((idH != 0x00050000) && (idH != 0x00050002) && (idH != 0))
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
                    menuItem[currItemNum].ID = id;
                    menuItem[currItemNum].type = type;
                    currItemNum++;
                }
            }
            movableItemsCount = currItemNum;

            if (!count)
            {
                qsort(menuItem, movableItemsCount, sizeof(struct MenuItemStruct), fSortCond);

                currItemNum = 0;
                for (int i = 0; i < maxItemsCount; i++)
                {
                    if (!moveableItem[i])
                        continue;
                    int itemOffset = i * 16 + folderOffset;
                    uint32_t idNAND = 0;
                    uint32_t idNANDh = 0;
                    uint32_t idUSB = 0;
                    uint32_t idUSBh = 0;
                    if (currItemNum < movableItemsCount)
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

                    itemOffset += usbOffset;

                    memcpy(fBuffer + itemOffset, &idUSBh, sizeof(uint32_t));
                    memcpy(fBuffer + itemOffset + 4, &idUSB, sizeof(uint32_t));
                    memset(fBuffer + itemOffset + 8, 0, 8);
                    fBuffer[itemOffset + 0x0b] = 2;
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
                deleteStaleHomebrewOnMenuPluginCache(userPersistentId);
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
