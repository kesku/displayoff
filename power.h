#ifndef DISPLAYOFF_POWER_H
#define DISPLAYOFF_POWER_H

#include <windows.h>

#define DISPLAYOFF_BLANK_MUTEX L"Local\\DisplayOff_NativeBlank"

DWORD displayoff_begin(void);
DWORD displayoff_restore(void);

#endif
