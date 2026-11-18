/* prefs.c — small persistent settings table.
 *
 * One text file, "MacintoshAI Prefs", in the Preferences folder (falling back
 * to the application's own folder on systems without one).  Format:
 *
 *     key=value\r
 *     key=value\r
 *
 * System 7 text files use CR line endings, which is also what SimpleText and
 * every text editor on the platform expect.
 *
 * This is the application's own prefs file, so it is written directly with
 * FSRead/FSWrite/FSpCreate rather than through the engine's prefs_io helper
 * (that one owns the TLS ticket cache format).
 */
#include <string.h>

#include <Files.h>
#include <Folders.h>

#include "prefs.h"

#define kPrefsMaxEntries 16
#define kPrefsKeyMax     32
#define kPrefsValueMax   256

#define kPrefsCreator 'McAI'
#define kPrefsType    'pref'

static char    sKeys[kPrefsMaxEntries][kPrefsKeyMax];
static char    sValues[kPrefsMaxEntries][kPrefsValueMax];
static short   sCount;
static Boolean sLoaded;

static FSSpec  sSpec;
static Boolean sHaveSpec;

/* "MacintoshAI Prefs" as a Pascal string, spelled out: the \p escape is an
   MPW extension the host build's compiler rejects. */
static const unsigned char kPrefsFileName[18] = {
    17, 'M','a','c','i','n','t','o','s','h','A','I',' ','P','r','e','f','s'
};

static void StoreAt(short i, const char *key, const char *value)
{
    strncpy(sKeys[i], key, kPrefsKeyMax - 1);
    sKeys[i][kPrefsKeyMax - 1] = '\0';
    strncpy(sValues[i], value, kPrefsValueMax - 1);
    sValues[i][kPrefsValueMax - 1] = '\0';
}

static short FindKey(const char *key)
{
    short i;

    if (!key)
        return -1;
    for (i = 0; i < sCount; i++) {
        if (strcmp(sKeys[i], key) == 0)
            return i;
    }
    return -1;
}

static OSErr PrefsPath(void)
{
    short vRefNum;
    long  dirID;
    OSErr err;

    if (sHaveSpec)
        return noErr;

    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                     &vRefNum, &dirID);
    if (err != noErr) {
        /* No Preferences folder (very old system): use the folder the app was
           launched from. */
        err = HGetVol(NULL, &vRefNum, &dirID);
        if (err != noErr)
            return err;
    }

    err = FSMakeFSSpec(vRefNum, dirID, (ConstStr255Param)kPrefsFileName, &sSpec);
    if (err == noErr)
        sHaveSpec = true;
    return err;
}

Boolean Prefs_Load(void)
{
    static char buf[3072];
    short refNum;
    long  count;
    char *p, *end;
    OSErr err;

    if (sLoaded)
        return true;

    sLoaded = true;      /* a missing file is not an error worth retrying */
    sCount = 0;

    if (PrefsPath() != noErr)
        return false;

    err = FSpOpenDF(&sSpec, fsRdPerm, &refNum);
    if (err != noErr)
        return false;

    count = (long)sizeof(buf) - 1;
    err = FSRead(refNum, &count, buf);
    FSClose(refNum);
    if (err != noErr && err != eofErr)
        return false;
    if (count <= 0)
        return true;

    buf[count] = '\0';
    p = buf;
    end = buf + count;
    while (p < end) {
        char *line = p;
        char *eq;

        while (p < end && *p != '\r' && *p != '\n')
            p++;
        if (p < end)
            *p++ = '\0';        /* terminate this line, step over the CR/LF */

        if (!*line || *line == '#')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (sCount < kPrefsMaxEntries)
            StoreAt(sCount++, line, eq + 1);
    }
    return true;
}

Boolean Prefs_Save(void)
{
    char  line[kPrefsKeyMax + kPrefsValueMax + 2];
    short refNum, i;
    long  len;
    OSErr err;

    if (!sLoaded)
        Prefs_Load();
    if (PrefsPath() != noErr)
        return false;

    err = FSpCreate(&sSpec, kPrefsCreator, kPrefsType, 0);
    if (err != noErr && err != dupFNErr)
        return false;

    err = FSpOpenDF(&sSpec, fsRdWrPerm, &refNum);
    if (err != noErr)
        return false;

    SetFPos(refNum, fsFromStart, 0);
    SetEOF(refNum, 0);

    for (i = 0; i < sCount; i++) {
        strcpy(line, sKeys[i]);
        strcat(line, "=");
        strcat(line, sValues[i]);
        strcat(line, "\r");
        len = (long)strlen(line);
        if (FSWrite(refNum, &len, line) != noErr) {
            FSClose(refNum);
            return false;
        }
    }

    FSClose(refNum);
    return true;
}

const char *Prefs_Get(const char *key, const char *fallback)
{
    short i;

    if (!sLoaded)
        Prefs_Load();
    i = FindKey(key);
    return (i >= 0) ? sValues[i] : fallback;
}

void Prefs_Set(const char *key, const char *value)
{
    short i;

    if (!sLoaded)
        Prefs_Load();
    if (!key || !*key)
        return;
    if (!value)
        value = "";

    i = FindKey(key);
    if (i >= 0) {
        StoreAt(i, key, value);
        return;
    }
    if (sCount < kPrefsMaxEntries)
        StoreAt(sCount++, key, value);
}
