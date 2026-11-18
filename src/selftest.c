/* selftest.c - drive the application from inside the guest and write a log.
 *
 * Why this exists: driving a Mac OS 9 guest from the host with synthetic mouse
 * and keyboard events is unreliable (a modal tracking loop loses the button-up)
 * and it moves the real cursor of whoever is using the Mac. This runs the
 * application's own UI code on a timer instead, and leaves a plain text log
 * where the harness can read it. No input events, no cursor, no OCR.
 *
 * The harness drops a file named "MacintoshAI selftest" into the clone's
 * System Folder:Preferences before boot and reads "MacintoshAI selftest log"
 * back afterwards.
 */

#include <Dialogs.h>
#include <Files.h>
#include <Folders.h>
#include <stdarg.h>
#include <stdio.h>
#include <Memory.h>
#include <Sound.h>
#include <string.h>
#include <Script.h>

#include "constants.h"
#include "error.h"
#include "selftest.h"
#include "chatbot/api_client.h"
#include "chatbot/model_manager.h"
#include "sound/beepbop.h"
#include "ui/chat_window.h"
#include "ui/window_manager.h"

static const unsigned char kTriggerName[] = "\pMacintoshAI selftest";
static const unsigned char kLogName[] = "\pMacintoshAI selftest log";

static short sLogRef = -1;

/* ---------------------------------------------------------------- */
/* Locating and writing the log                                      */
/* ---------------------------------------------------------------- */

/* The log lives next to the preferences, which is where the trigger is put.
 * FindFolder creates the folder if a fresh volume has none. */
static Boolean FindPrefsFolder(FSSpec *spec, ConstStr255Param name)
{
    short vRefNum;
    long dirID;
    OSErr err;

    if (spec == NULL) {
        return false;
    }

    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                     &vRefNum, &dirID);
    if (err != noErr) {
        return false;
    }

    err = FSMakeFSSpec(vRefNum, dirID, name, spec);
    return (err == noErr || err == fnfErr);
}

Boolean SelfTest_Requested(void)
{
    FSSpec spec;

    if (!FindPrefsFolder(&spec, kTriggerName)) {
        return false;
    }
    /* fnfErr produced a spec for a file that does not exist; creating the spec
     * only succeeds with noErr when the file is really there. */
    return FSMakeFSSpec(spec.vRefNum, spec.parID, kTriggerName, &spec) == noErr;
}

/* Append one line. Deliberately no stdio: this runs on 68k with a tiny heap,
 * and vsnprintf's behaviour there is one more variable than the test needs. */
static void LogRaw(const char *text)
{
    long count;

    if (sLogRef < 0 || text == NULL) {
        return;
    }

    count = (long)strlen(text);
    if (count <= 0) {
        return;
    }
    SetFPos(sLogRef, fsFromLEOF, 0);
    FSWrite(sLogRef, &count, (Ptr)text);

    /* The harness kills the VM and then mounts the clone, so the bytes have to
     * be on the volume, not only in the guest's cache. FlushVol on its own left
     * the log a run of zero bytes; flushing this file's own blocks does it. */
    {
        ParamBlockRec pb;

        memset(&pb, 0, sizeof(pb));
        pb.ioParam.ioRefNum = sLogRef;
        PBFlushFileSync((ParmBlkPtr)&pb);
    }
}

static char *AppendStr(char *p, const char *text)
{
    if (text == NULL) {
        text = "(null)";
    }
    while (*text != '\0') {
        *p++ = *text++;
    }
    return p;
}

static char *AppendNum(char *p, long value)
{
    char digits[12];
    short n = 0;
    Boolean negative = false;

    if (value < 0) {
        negative = true;
        value = -value;
    }
    do {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && n < 11);

    if (negative) {
        *p++ = '-';
    }
    while (n > 0) {
        *p++ = digits[--n];
    }
    return p;
}

/* log_line("open_chat=", 1, NULL) */
static void LogLine(const char *a, const char *b, const char *c, long num,
                    Boolean withNum)
{
    char line[320];
    char *p = line;

    p = AppendStr(p, a);
    if (b != NULL) {
        p = AppendStr(p, b);
    }
    if (c != NULL) {
        p = AppendStr(p, c);
    }
    if (withNum) {
        p = AppendNum(p, num);
    }
    *p++ = '\r';
    *p = '\0';
    LogRaw(line);
}

static Boolean OpenLog(void)
{
    FSSpec spec;
    OSErr err;

    if (!FindPrefsFolder(&spec, kLogName)) {
        return false;
    }

    err = FSpCreate(&spec, 'McAI', 'TEXT', smSystemScript);
    if (err != noErr && err != dupFNErr) {
        return false;
    }

    err = FSpOpenDF(&spec, fsRdWrPerm, &sLogRef);
    if (err != noErr) {
        sLogRef = -1;
        return false;
    }

    SetEOF(sLogRef, 0);
    return true;
}

static void CloseLog(void)
{
    if (sLogRef >= 0) {
        FSClose(sLogRef);
        sLogRef = -1;
    }
}

/* ---------------------------------------------------------------- */
/* Running the UI                                                    */
/* ---------------------------------------------------------------- */

/* The same work the main loop does, without the event dispatch we do not want
 * (a stray click could not arrive anyway — nothing is posted). */
static void Pump(unsigned long ticks)
{
    unsigned long until = TickCount() + ticks;

    while (TickCount() < until) {
        EventRecord event;

        SystemTask();
        BeepBopIdle();
        ChatWindow_Idle();

        /* Dispatch, not discard: update events are what make the window
         * manager paint. A pump that swallowed them produced blank windows. */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            WindowManager_HandleEvent(&event);
        }
    }
}

void SelfTest_Run(void)
{
    char transcript[512];
    char line[128];

    if (!OpenLog()) {
        return;
    }

    LogLine("macintosh-ai selftest ", APP_VERSION_STR, NULL, 0, false);
    LogLine("model=", NULL, NULL, (long)gActiveAIModel, true);
    LogLine("provider=", NULL, NULL, (long)API_GetProvider(), true);
    LogLine("key=", NULL, NULL,
            (long)(API_GetAPIKey() != NULL && API_GetAPIKey()[0] != '\0'), true);
    LogLine("baseurl=", API_GetBaseURL(), NULL, 0, false);
    LogLine("model_name=", API_GetModel(), NULL, 0, false);
    LogLine("splash_visible=", NULL, NULL,
            (long)WindowManager_IsWindowVisible(kWindowTypeSplash), true);

    Pump(30);

    /* Open the chat window the way the Start Chat button does. */
    LogLine("open_chat=", NULL, NULL,
            (long)WindowManager_OpenWindow(kWindowTypeChat), true);
    Pump(60);
    LogLine("chat_visible=", NULL, NULL,
            (long)WindowManager_IsWindowVisible(kWindowTypeChat), true);

    /* Type and submit: the prompt goes through the same input and send path a
     * user's typing takes. */
    LogLine("submit=", NULL, NULL, (long)ChatWindow_SubmitForTest("hello"), true);
    Pump(600);

    ChatWindow_TranscriptForTest(transcript, 180);
    LogLine("transcript=", transcript, NULL, 0, false);
    LogLine("api_state=", NULL, NULL, (long)API_State(), true);
    LogLine("api_error=", API_LastError(), NULL, 0, false);

    /* One more round trip to show the state machine recovers. */
    ChatWindow_SubmitForTest("and again");
    Pump(300);
    ChatWindow_TranscriptForTest(transcript, 180);
    LogLine("transcript2=", transcript, NULL, 0, false);
    LogLine("api_state2=", NULL, NULL, (long)API_State(), true);
    LogLine("api_error2=", API_LastError(), NULL, 0, false);

    LogLine("done", NULL, NULL, 0, false);
    CloseLog();

    /* Last chance to get the bytes onto the volume before the process goes. */
    FlushVol(NULL, 0);

    API_Shutdown();
    ExitToShell();
}
