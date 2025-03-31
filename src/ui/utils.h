#ifndef UTILS_H
#define UTILS_H

#include <Controls.h>
#include <TextEdit.h>
#include <Windows.h>

/* Utility functions */

/* Create a standard button */
ControlHandle CreateStandardButton(WindowRef window, Rect *bounds, StringPtr title,
                                   short controlID);

/* Create a standard text field */
TEHandle CreateStandardTextField(Rect *viewRect, Rect *destRect, Boolean isEditable);

/* Draw a standard window frame */
void DrawStandardFrame(WindowRef window);

/* Update TextEdit scrollbar relationship */
void UpdateTextScrollbar(TEHandle textHandle, ControlHandle scrollBar, Boolean scrollToBottom);

/* Apply standard text formatting for chat UI */
void ApplyTextFormatting(short font, short size, short style);

/* Draw a background gradient */
void DrawBackgroundGradient(WindowRef window, short height);

/* Trim leading and trailing whitespace (spaces, tabs, newlines, carriage returns) from a string */
void TrimWhitespace(char *str);

/* Convert a string to lowercase (modifies the string in place) */
void ConvertToLowercase(char *str);

#endif /* UTILS_H */