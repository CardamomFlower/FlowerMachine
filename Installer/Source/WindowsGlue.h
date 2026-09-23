#pragma once

/*  The one place that includes <windows.h>.

    JUCE guards the platform headers before including them, and the same guards are
    needed here: without NOMINMAX and WIN32_LEAN_AND_MEAN, wingdi.h's Rectangle, the
    min/max macros, the A/W macros for DrawText and friends and rpcndr.h's `small`
    all collide with JUCE names in a GUI translation unit.

    Include this BEFORE JuceHeader.h in the few .cpp files that need Win32 directly;
    no header in this project may include it.
*/

#ifndef NOMINMAX
 #define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef STRICT
 #define STRICT 1
#endif
#ifndef UNICODE
 #define UNICODE 1
#endif
#ifndef _UNICODE
 #define _UNICODE 1
#endif

#include <windows.h>
#include <shlobj.h>
#include <objidl.h>

#undef small
#undef GetObject
#undef DrawText
#undef SendMessage
#undef PostMessage
#undef CreateWindow
#undef SetPort
