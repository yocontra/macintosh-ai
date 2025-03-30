#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "error.h"

/* Window dimensions */
#define WINDOW_WIDTH 510
#define WINDOW_HEIGHT 302

/* Only define implementation in this file - IMPORTANT! */
#define NK_ZERO_COMMAND_MEMORY
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_QUICKDRAW_IMPLEMENTATION
#define NK_MEMSET memset
#define NK_MEMCPY memcpy
#define NK_ASSERT(e)                                                                               \
    if (!(e))                                                                                      \
    HandleError(kErrNuklearAssertion, kCtxLaunchingApp, true)

#include <Types.h>
#include <Resources.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <Sound.h>
#include <SegLoad.h>
#include <Files.h>
#include <OSUtils.h>
#include <DiskInit.h>
#include <Packages.h>
#include <Traps.h>

#include "nuklear/nuklear.h"
#include "nuklear/nuklear_quickdraw.h"

#include "ai_model.h"
#include "constants.h"
#include "ui/chat_window.h"
#include "ui/splash_window.h"
#include "ui/menu.h"
#include "ui/event.h"

/* Application mode tracking */
short gAppMode = kModeMainSplash;

/* Shared nuklear state */
struct nk_context *ctx;     /* Main nuklear context */
Boolean redraw_needed = true;  /* Flag to indicate if we need to redraw the UI */
Point last_mouse_pos;      /* Last mouse position for tracking movement */
static int window_width = WINDOW_WIDTH;
static int window_height = WINDOW_HEIGHT;

/* Environment variables */
SysEnvRec gMac;                     /* System environment record */
Boolean gHasWaitNextEvent = false;  /* WaitNextEvent availability flag */
Boolean gInBackground = false;      /* Are we in the background? */
Boolean gotMouseEvent = false;      /* Did we get a mouse event? */
Boolean gotKeyboardEvent = false;   /* Did we get a keyboard event? */
int gotKeyboardEventTime = 0;       /* Time we got the keyboard event */

/* Function prototypes */
void EventLoop(struct nk_context *ctx);
static void DoEvent(EventRecord *event, struct nk_context *ctx);
void GetGlobalMouse(Point *mouse);
static void DoUpdate(WindowPtr window);
static void DoActivate(WindowPtr window, Boolean becomingActive);
static void AdjustMenus(void);
static void HandleMenuCommand(long menuResult);
static Boolean DoCloseWindow(WindowPtr window);
void Terminate(void);
void Initialize(void);
Boolean IsAppWindow(WindowPtr window);
Boolean IsDAWindow(WindowPtr window);
Boolean TrapAvailable(short tNumber, TrapType tType);

/* Define HiWrd and LoWrd macros for efficiency */
#define HiWrd(aLong)    (((aLong) >> 16) & 0xFFFF)
#define LoWrd(aLong)    ((aLong) & 0xFFFF)

/* Define TopLeft and BotRight macros for convenience */
#define TopLeft(aRect)  (* (Point *) &(aRect).top)
#define BotRight(aRect) (* (Point *) &(aRect).bottom)

/* Main entry point */
#pragma segment Main
int main(void)
{
    Initialize();                 /* Initialize the program */
    UnloadSeg((Ptr) Initialize);  /* Free initialization code segment */

    /* Initialize our Nuklear UI context */
    ctx = nk_quickdraw_init(window_width, window_height);
    if (ctx == NULL) {
        HandleError(kErrMemoryFull, kCtxLaunchingApp, true); /* Fatal error */
        return 1; /* Should never reach here due to fatal error */
    }
    
    /* Use default Nuklear styling */

    /* Initialize conversation history */
    InitConversationHistory();

    /* Initialize only the splash window */
    SplashWindow_Initialize();

    /* Show the splash window initially */
    SplashWindow_Show(true);
    gAppMode = kModeMainSplash;

    /* Start the main event loop */
    EventLoop(ctx);
    
    /* Should never reach here */
    return 0;
}

/* Main event loop - handles all user input and rendering */
#pragma segment Main
void EventLoop(struct nk_context *ctx)
{
    RgnHandle cursorRgn;
    Boolean gotEvent;
    EventRecord event;
    Point mouse;
    
    cursorRgn = NewRgn();

    int lastMouseHPos = 0;
    int lastMouseVPos = 0;
    int lastUpdatedTickCount = 0;
    Boolean firstOrMouseMove = true;
    gotMouseEvent = false;

    do {
        /* Check if keyboard timeout has expired */
        if (gotKeyboardEvent && TickCount() > gotKeyboardEventTime + 20) {
            gotKeyboardEvent = false;
            ShowCursor();
        }

        Boolean beganInput = false;

        /* Get current mouse position */
        GetGlobalMouse(&mouse);

        /* Check for mouse movement */
        if (lastMouseHPos != mouse.h || lastMouseVPos != mouse.v) {
            /* Process all mouse motion before rendering */
            while (lastMouseHPos != mouse.h || lastMouseVPos != mouse.v) {
                Point tempPoint;
                SetPt(&tempPoint, mouse.h, mouse.v);
                GlobalToLocal(&tempPoint);

                if (!beganInput) {
                    nk_input_begin(ctx);
                }

                nk_input_motion(ctx, tempPoint.h, tempPoint.v);

                firstOrMouseMove = true;
                beganInput = true;

                lastUpdatedTickCount = TickCount();
                lastMouseHPos = mouse.h;
                lastMouseVPos = mouse.v;
                GetGlobalMouse(&mouse);
            }
        } else {
            /* Get the next event */
            gotEvent = GetNextEvent(everyEvent, &event);

            /* Process all events before rendering */
            while (gotEvent) {
                lastUpdatedTickCount = TickCount();

                if (!beganInput) {
                    nk_input_begin(ctx);
                    beganInput = true;
                }

                DoEvent(&event, ctx);

                if (!gotMouseEvent) {
                    gotEvent = GetNextEvent(everyEvent, &event);
                } else {
                    gotEvent = false;
                }
            }
        }

        lastMouseHPos = mouse.h;
        lastMouseVPos = mouse.v;

        SystemTask();

        /* Render the UI if there was input or mouse movement */
        if (beganInput || firstOrMouseMove) {
            nk_input_end(ctx);
            firstOrMouseMove = false;

            /* Update active window based on application mode */
            if (gAppMode == kModeMainSplash) {
                SplashWindow_Update();
            } else if (gAppMode == kModeChatWindow) {
                ChatWindow_Update();
            }

            GetGlobalMouse(&mouse);

            /* Only render if mouse isn't moving */
            if (lastMouseVPos == mouse.v || lastMouseHPos == mouse.h) {
                nk_quickdraw_render(FrontWindow(), ctx);
            }

            nk_clear(ctx);
        }
    } while (true); /* Loop forever until ExitToShell() */
}

/* Process individual events */
#pragma segment Main
static void DoEvent(EventRecord *event, struct nk_context *ctx)
{
    short part;
    short err;
    WindowPtr window;
    char key;
    Point aPoint;

    switch (event->what) {
        case mouseUp:
            part = FindWindow(event->where, &window);
            switch (part) {
                case inContent:
                    nk_quickdraw_handle_event(event, ctx);
                    break;
                default:
                    break;
            }
            break;
            
        case mouseDown:
            gotMouseEvent = true;

            part = FindWindow(event->where, &window);
            switch (part) {
                case inMenuBar:
                    AdjustMenus();
                    HandleMenuCommand(MenuSelect(event->where));
                    break;
                    
                case inSysWindow:
                    SystemClick(event, window);
                    break;
                    
                case inContent:
                    if (window != FrontWindow()) {
                        SelectWindow(window);
                    }
                    
                    /* Pass click to Nuklear */
                    nk_quickdraw_handle_event(event, ctx);
                    
                    /* Also check for specific window logic */
                    if (window == SplashWindow_GetWindowRef()) {
                        Point mousePt = event->where;
                        GlobalToLocal(&mousePt);
                        
                        /* Check if start chat button was clicked */
                        if (SplashWindow_HandleContentClick(mousePt)) {
                            /* Initialize and open chat window if button was clicked */
                            ChatWindow_Initialize();
                            ChatWindow_Show(true);
                            SplashWindow_Show(false);
                            gAppMode = kModeChatWindow;
                        }
                    }
                    break;
                    
                case inDrag:
                    DragWindow(window, event->where, &qd.screenBits.bounds);
                    break;
                    
                case inGoAway:
                    if (TrackGoAway(window, event->where)) {
                        if (window == SplashWindow_GetWindowRef()) {
                            /* Closing main window quits app */
                            Terminate();
                        } else if (window == ChatWindow_GetWindowRef()) {
                            /* Closing chat window returns to splash screen */
                            ChatWindow_Show(false);
                            SplashWindow_Show(true);
                            gAppMode = kModeMainSplash;
                        } else {
                            DisposeWindow(window);
                        }
                    }
                    break;
            }
            break;
            
        case keyDown:
        case autoKey:
            if (!gotKeyboardEvent) {
                HideCursor();
                gotKeyboardEvent = true;
            }

            gotKeyboardEventTime = TickCount();

            key = event->message & charCodeMask;
            if (event->modifiers & cmdKey) {
                if (event->what == keyDown) {
                    /* Command key shortcuts */
                    if (key == 'l' || key == 'L') {
                        /* Cmd-L: Open chat window (from any mode) */
                        if (ChatWindow_GetWindowRef() == NULL) {
                            ChatWindow_Initialize();
                        }
                        ChatWindow_Show(true);
                        SplashWindow_Show(false);
                        gAppMode = kModeChatWindow;
                    } else {
                        /* Other command key combinations */
                        AdjustMenus();
                        HandleMenuCommand(MenuKey(key));
                    }
                }
            } else {
                /* Regular keypress - handle with Nuklear */
                nk_quickdraw_handle_event(event, ctx);
                
                /* Also check for specific window logic */
                if (gAppMode == kModeChatWindow) {
                    if (key == '\r' || key == 3) { /* Return key or Enter key */
                        ChatWindow_ProcessReturnKey();
                    }
                }
            }
            break;
            
        case activateEvt:
            window = (WindowPtr) event->message;
            DoActivate(window, (event->modifiers & activeFlag) != 0);
            break;
            
        case updateEvt:
            DoUpdate((WindowPtr) event->message);
            break;
            
        case diskEvt:
            if (HiWrd(event->message) != noErr) {
                SetPt(&aPoint, 50, 50); /* Arbitrary position for disk initialization dialog */
                err = DIBadMount(aPoint, event->message);
            }
            break;
            
        case osEvt:
            /* Check for suspend/resume events */
            if (((event->message >> 24) & 0xFF) == suspendResumeMessage) {
                gInBackground = ((event->message & resumeFlag) == 0);
                DoActivate(FrontWindow(), !gInBackground);
            }
            break;
    }
}

/* Get the global coordinates of the mouse */
#pragma segment Main
void GetGlobalMouse(Point *mouse)
{
    EventRecord event;
    
    OSEventAvail(0, &event);  /* Just get mouse position, don't consume events */
    *mouse = event.where;
}

/* Handle window update events */
#pragma segment Main
static void DoUpdate(WindowPtr window)
{
    if (IsAppWindow(window)) {
        BeginUpdate(window);
        
        /* Update the appropriate window */
        if (window == SplashWindow_GetWindowRef()) {
            SplashWindow_Update();
        } else if (window == ChatWindow_GetWindowRef()) {
            ChatWindow_Update();
        }
        
        EndUpdate(window);
    }
}

/* Handle window activation/deactivation */
#pragma segment Main
static void DoActivate(WindowPtr window, Boolean becomingActive)
{
    if (IsAppWindow(window)) {
        if (window == ChatWindow_GetWindowRef()) {
            ChatWindow_HandleActivate(becomingActive);
        }
    }
}

/* Enable and disable menus based on current state */
#pragma segment Main
static void AdjustMenus(void)
{
    MenuHandle menu;
    
    /* Update File menu */
    menu = GetMenuHandle(kMenuFile);
    if (menu != NULL) {
        EnableItem(menu, kItemQuit);
    }
}

/* Handle menu command selection */
#pragma segment Main
static void HandleMenuCommand(long menuResult)
{
    short menuID = HiWrd(menuResult);
    short menuItem = LoWrd(menuResult);
    
    /* Process menu selection based on ID and item */
    switch (menuID) {
        case kMenuApple:
            /* Apple menu item handling */
            break;
            
        case kMenuFile:
            /* File menu item handling */
            if (menuItem == kItemQuit) {
                Terminate();
            }
            break;
    }
    
    HiliteMenu(0);  /* Unhighlight menu title */
}

/* Close a window */
#pragma segment Main
static Boolean DoCloseWindow(WindowPtr window)
{
    if (IsDAWindow(window)) {
        /* System 7 uses CloseWindow for desk accessories */
        CloseWindow(window);
    } else if (IsAppWindow(window)) {
        /* Handle specific window closing */
        if (window == SplashWindow_GetWindowRef()) {
            Terminate();
        } else if (window == ChatWindow_GetWindowRef()) {
            ChatWindow_Show(false);
            SplashWindow_Show(true);
            gAppMode = kModeMainSplash;
        } else {
            CloseWindow(window);
        }
    }
    return true;
}

/* Clean up and exit the application */
#pragma segment Main
void Terminate(void)
{
    WindowPtr aWindow;
    Boolean closed;
    
    /* First, properly dispose of our windows */
    if (SplashWindow_GetWindowRef() != NULL) {
        SplashWindow_Show(false);
        SplashWindow_Dispose();
    }
    
    if (ChatWindow_GetWindowRef() != NULL) {
        ChatWindow_Show(false);
        ChatWindow_Dispose();
    }
    
    /* Properly shutdown and free the Nuklear context memory */
    if (ctx != NULL) {
        nk_quickdraw_shutdown();
        ctx = NULL;
    }
    
    /* Now close any remaining windows */
    closed = true;
    do {
        aWindow = FrontWindow();
        if (aWindow != NULL) {
            closed = DoCloseWindow(aWindow);
        }
    } while (closed && (aWindow != NULL));
    
    /* Finally, exit */
    if (closed) {
        ExitToShell();
    }
}

/* Initialize the application */
#pragma segment Initialize
void Initialize(void)
{
    Handle menuBar;
    
    gInBackground = false;
    
    /* Initialize Mac Toolbox managers */
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    
    /* Clear any pending events */
    FlushEvents(everyEvent, 0);
    
    /* Set up menu bar */
    menuBar = GetNewMBar(128);
    if (menuBar == NULL) {
        HandleError(kErrResourceNotFound, kCtxLaunchingApp, true);
    }
    
    SetMenuBar(menuBar);
    DisposeHandle(menuBar);
    
    /* Get Apple menu and append desk accessories */
    MenuHandle appleMenu = GetMenu(kMenuApple);
    if (appleMenu == NULL) {
        HandleError(kErrResourceNotFound, kCtxLaunchingApp, true);
    }
    
    AppendResMenu(appleMenu, 'DRVR');
    DrawMenuBar();
}

/* Check if window is an application window */
#pragma segment Main
Boolean IsAppWindow(WindowPtr window)
{
    short windowKind;
    
    if (window == NULL) {
        return false;
    }
    
    windowKind = ((WindowPeek) window)->windowKind;
    return (windowKind == userKind);
}

/* Check if window is a desk accessory window */
#pragma segment Main
Boolean IsDAWindow(WindowPtr window)
{
    if (window == NULL) {
        return false;
    }
    
    return ((WindowPeek) window)->windowKind < 0;
}

/* Check if a trap is available */
#pragma segment Initialize
Boolean TrapAvailable(short tNumber, TrapType tType)
{
    if ((tType == ToolTrap) &&
        (gMac.machineType > envMachUnknown) &&
        (gMac.machineType < envMacII)) {
        tNumber = tNumber & 0x03FF;
        if (tNumber > 0x01FF) {
            tNumber = _Unimplemented;
        }
    }
    
    return NGetTrapAddress(tNumber, tType) != GetTrapAddress(_Unimplemented);
}