#include <Controls.h>
#include <Fonts.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>
#include <string.h>

#include "../constants.h"
#include "../error.h"
#include "utils.h"

/* Create a standard button with consistent styling */
ControlHandle CreateStandardButton(WindowRef window, Rect *bounds, StringPtr title, short controlID)
{
    if (window == NULL || bounds == NULL) {
        return NULL;
    }

    return NewControl(window, bounds, title, true, 0, 0, 1, pushButProc, (long)controlID);
}

/* Create a standard text field */
TEHandle CreateStandardTextField(Rect *viewRect, Rect *destRect, Boolean isEditable)
{
    TEHandle teHandle;

    if (viewRect == NULL || destRect == NULL) {
        return NULL;
    }

    teHandle = TENew(destRect, viewRect);
    if (teHandle == NULL) {
        return NULL;
    }

    /* Set up text field properties */
    if (isEditable) {
        TEActivate(teHandle);
        TEFeatureFlag(teFAutoScroll, teBitSet, teHandle);
    }
    else {
        TEDeactivate(teHandle);
        TEFeatureFlag(teFAutoScroll, teBitClear, teHandle);
    }

    return teHandle;
}

/* Draw a standard window frame */
void DrawStandardFrame(WindowRef window)
{
    if (window == NULL) {
        return;
    }

    SetPort(window);

    /* Erase window with white background */
    BackColor(whiteColor);
    EraseRect(&window->portRect);

    /* Draw frame if needed */
    PenNormal();
    PenSize(1, 1);
}

/* Update TextEdit scrollbar relationship */
void UpdateTextScrollbar(TEHandle textHandle, ControlHandle scrollBar, Boolean scrollToBottom)
{
    short viewHeight, textHeight, maxScroll, scrollPos;

    if (scrollBar == NULL || textHandle == NULL || *textHandle == NULL) {
        return;
    }

    /* Calculate scroll values */
    viewHeight = (*textHandle)->viewRect.bottom - (*textHandle)->viewRect.top;
    textHeight = TEGetHeight(0, (*textHandle)->teLength, textHandle);

    /* Calculate maximum scroll value */
    maxScroll = 0;
    if (textHeight > viewHeight) {
        maxScroll = textHeight - viewHeight + 3; /* Add small buffer */
    }

    /* Determine scroll position */
    if (scrollToBottom) {
        scrollPos = maxScroll;
    }
    else {
        scrollPos = GetControlValue(scrollBar);
        if (scrollPos > maxScroll) {
            scrollPos = maxScroll;
        }
    }

    /* Update scrollbar state */
    if (maxScroll <= 0) {
        /* Disable scrollbar when no scrolling needed */
        HiliteControl(scrollBar, 255);
        SetControlMaximum(scrollBar, 0);
        SetControlValue(scrollBar, 0);
    }
    else {
        /* Enable scrollbar and set values */
        HiliteControl(scrollBar, 0);
        SetControlMaximum(scrollBar, maxScroll);
        SetControlValue(scrollBar, scrollPos);
    }

    /* Update text position */
    short currentPos = (*textHandle)->viewRect.top - (*textHandle)->destRect.top;
    short delta      = currentPos - scrollPos;

    if (delta != 0) {
        TEScroll(0, delta, textHandle);
    }
}

/* Apply standard text formatting for UI */
void ApplyTextFormatting(short font, short size, short style)
{
    TextFont(font);
    TextSize(size);
    TextFace(style);
    ForeColor(blackColor);
    BackColor(whiteColor);
}

/* Draw a background gradient */
void DrawBackgroundGradient(WindowRef window, short height)
{
    Pattern theBits;
    Rect frameRect;
    short i, j;

    if (window == NULL || height <= 0) {
        return;
    }

    SetPort(window);
    frameRect = window->portRect;

    /* Create visual gradient for background */
    for (i = 0; i < height; i++) {
        /* Create a pattern for this row */
        for (j = 0; j < 8; j++) {
            theBits.pat[j] = (i % 4 == 0) ? 0xAA : ((i % 4 == 1) ? 0x55 : 0);
        }

        /* Set the pattern and draw a line */
        PenPat(&theBits);
        MoveTo(frameRect.left, frameRect.top + i);
        LineTo(frameRect.right, frameRect.top + i);
    }

    /* Reset pen state */
    PenNormal();
}

/* Trim leading and trailing whitespace */
void TrimWhitespace(char *str)
{
    if (str == NULL)
        return;

    char *start = str;
    char *end;
    size_t len;

    /* If string is empty, nothing to do */
    if (str[0] == '\0')
        return;

    /* Find first non-whitespace character */
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
        start++;

    /* If string is all whitespace, clear it and return */
    if (*start == '\0') {
        str[0] = '\0';
        return;
    }

    /* Find the end of the string */
    len = strlen(start);
    end = start + len - 1;

    /* Trim trailing whitespace */
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;

    /* Terminate string after last non-whitespace character */
    *(end + 1) = '\0';

    /* If start is different from original string, move the trimmed string to the beginning */
    if (start != str) {
        len = end - start + 1;
        memmove(str, start, len + 1); /* +1 for null terminator */
    }
}

/* Convert a string to lowercase (modifies the string in place) */
void ConvertToLowercase(char *str)
{
    if (str == NULL)
        return;

    while (*str) {
        if (*str >= 'A' && *str <= 'Z')
            *str = *str + 32;
        str++;
    }
}