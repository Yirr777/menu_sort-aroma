#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <coreinit/dynload.h>
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

#define TITLE_TEXT "Sort Wii U Menu v1.2.0 (Aroma) - Yardape8000 & doino-gretchenliev"
#define HBL_TITLE_ID 0x13374842
#define MAX_ITEMS_COUNT 300

static const char *cafeXmlPath = "storage_slc:/proc/prefs/cafe.xml";
static const char *syshaxXmlPath = "storage_slc:/config/syshax.xml";
static const char *systemXmlPath = "storage_slc:/config/system.xml";
static const char *dontmovePath = "fs:/vol/external01/wiiu/apps/menu_sort/dontmove";
static const char *gamemapPath = "fs:/vol/external01/wiiu/apps/menu_sort/titlesmap";
static const char *backupPath = "fs:/vol/external01/wiiu/apps/menu_sort/BaristaAccountSaveFile.dat";
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
    screenClear();
    screenPrint(TITLE_TEXT);
    screenPrint("Choose sorting method:");
    screenPrint("Press 'B' for standard sorting.");
    screenPrint("Press 'A' for standard sorting(ignoring leading 'The').");
    screenPrint("Press 'X' for bad naming mode sorting.");
    screenPrint("Press 'Y' for bad naming mode sorting(ignoring leading 'The').");
    screenPrint("Press '+' for backup of the current order(incl. folders).");
    screenPrint("Press '-' for restore of the current order(incl. folders).");
    screenPrint("Press 'L' for counting the items only(no changes).");

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

    screenClear();
    screenSetPrintLine(0);
    screenPrint(TITLE_TEXT);
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

    screenPrintAt(strlen(modeSelectedText), screenGetPrintLine(), "done.");

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
    screenPrint("Press Home to exit");

    while (WHBProcIsRunning())
    {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
        uint32_t pressedBtns = 0;

        if (vpadError == VPAD_READ_SUCCESS)
            pressedBtns = vpad.trigger | vpad.hold;

        if (pressedBtns & VPAD_BUTTON_HOME)
            break;

        OSSleepTicks(OSMillisecondsToTicks(1));
    }
    failed = 0;

prgEnd:
    if (failed && failError[0])
    {
        screenPrint(failError);
        OSSleepTicks(OSSecondsToTicks(5));
    }

    if (fBuffer)
        free(fBuffer);

    if (fsMounted)
    {
        Mocha_UnmountFS("storage_slc");
        Mocha_UnmountFS("storage_mlc");
        Mocha_UnmountFS("storage_usb");
        Mocha_DeInitLibrary();
    }

    screenShutdown();
    WHBProcShutdown();

    return 0;
}
