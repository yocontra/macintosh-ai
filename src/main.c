#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Fonts.h>
#include <Resources.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Dialogs.h>
#include <Devices.h>

static Rect initialWindowRect, nextWindowRect;

enum
{
    kMenuApple = 128,
    kMenuFile,
    kMenuEdit
};

enum
{
    kItemAbout = 1,

    kItemNewDoc = 1,
    kItemClose = 2,
    kItemQuit = 4
};

void MakeNewWindow()
{
    Str255 title;
    GetIndString(title, 128, 1);

    if(nextWindowRect.bottom > qd.screenBits.bounds.bottom
        || nextWindowRect.right > qd.screenBits.bounds.right)
    {
        nextWindowRect = initialWindowRect;
    }

    WindowRef w = NewWindow(NULL, &nextWindowRect, title, true, documentProc, (WindowPtr) -1, true, 0);

    OffsetRect(&nextWindowRect, 15, 15);
}

void ShowAboutBox(void)
{
    WindowRef w = GetNewWindow(128, NULL, (WindowPtr) - 1);
    MoveWindow(w,
        qd.screenBits.bounds.right/2 - w->portRect.right/2,
        qd.screenBits.bounds.bottom/2 - w->portRect.bottom/2,
        false);
    ShowWindow(w);
    SetPort(w);

    Handle h = GetResource('TEXT', 128);
    HLock(h);
    Rect r = w->portRect;
    InsetRect(&r, 10,10);
    TETextBox(*h, GetHandleSize(h), &r, teJustLeft);

    ReleaseResource(h);
    while(!Button())
        ;
    while(Button())
        ;
    FlushEvents(everyEvent, 0);
    DisposeWindow(w);
}

void UpdateMenus(void)
{
    MenuRef m = GetMenu(kMenuFile);
    WindowRef w = FrontWindow();
    if(w) // Close menu item: enabled if there is a window
        EnableItem(m,kItemClose);
    else
        DisableItem(m,kItemClose);

    m = GetMenu(kMenuEdit);
    if(w && GetWindowKind(w) < 0)
    {  
        // Desk accessory in front: Enable edit menu items
        EnableItem(m,1);
        EnableItem(m,3);
        EnableItem(m,4);
        EnableItem(m,5);
        EnableItem(m,6);
    }
    else
    {   
        // Application window or nothing in front, disable edit menu
        DisableItem(m,1);
        DisableItem(m,3);
        DisableItem(m,4);
        DisableItem(m,5);
        DisableItem(m,6);
    }

}

void DoMenuCommand(long menuCommand)
{
    Str255 str;
    WindowRef w;
    short menuID = menuCommand >> 16;
    short menuItem = menuCommand & 0xFFFF;
    if(menuID == kMenuApple)
    {
        if(menuItem == kItemAbout)
            ShowAboutBox();
        else
        {
            GetMenuItemText(GetMenu(128), menuItem, str);
            OpenDeskAcc(str);
        }
    }
    else if(menuID == kMenuFile)
    {
        switch(menuItem)
        {
            case kItemNewDoc:
                MakeNewWindow();
                break;

            case kItemClose:    // close
                w = FrontWindow();
                if(w)
                {
                    if(GetWindowKind(w) < 0)
                        CloseDeskAcc(GetWindowKind(w));
                    else
                        DisposeWindow(FrontWindow());
                }
                break;

            case kItemQuit:
                ExitToShell();
                break;
        }
    }
    else if(menuID == kMenuEdit)
    {
        if(!SystemEdit(menuItem - 1))
        {
            // edit command not handled by desk accessory
        }
    }

    HiliteMenu(0);
}

void DoUpdate(WindowRef w)
{
    SetPort(w);
    BeginUpdate(w);

    Rect r;
    SetRect(&r, 20,20,120,120);
    FrameOval(&r);

    OffsetRect(&r, 32, 32);
    FillRoundRect(&r, 16, 16, &qd.ltGray);
    FrameRoundRect(&r, 16, 16);

    OffsetRect(&r, 32, 32);
    FillRect(&r, &qd.gray);
    FrameRect(&r);

    MoveTo(100,100);
    DrawString("\pHello, world.");

    EndUpdate(w);
}

int main(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);

    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenu(128), 'DRVR');
    DrawMenuBar();

    InitCursor();

    Rect r;
    SetRect(&initialWindowRect,20,60,400,260);
    nextWindowRect = initialWindowRect;

    MakeNewWindow();

    for(;;)
    {
        EventRecord e;
        WindowRef win;
        
        SystemTask();
        if(GetNextEvent(everyEvent, &e))
        {
            switch(e.what)
            {
                case keyDown:
                    if(e.modifiers & cmdKey)
                    {
                        UpdateMenus();
                        DoMenuCommand(MenuKey(e.message & charCodeMask));
                    }
                    break;
                case mouseDown:
                    switch(FindWindow(e.where, &win))
                    {
                        case inGoAway:
                            if(TrackGoAway(win, e.where))
                                DisposeWindow(win);
                            break;
                        case inDrag:
                            DragWindow(win, e.where, &qd.screenBits.bounds);
                            break;
                        case inMenuBar:
                            UpdateMenus();
                            DoMenuCommand( MenuSelect(e.where) );
                            break;
                        case inContent:
                            SelectWindow(win);
                            break;
                        case inSysWindow:
                            SystemClick(&e, win);
                            break;
                    }
                    break;
                case updateEvt:
                    DoUpdate((WindowRef)e.message);
                    break;
            }
        }
    }
    return 0;
}
