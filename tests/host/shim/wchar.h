/*
 * wchar.h  --  Host build shim
 * ----------------------------
 * Several core files do `#include <wchar.h>` to reach the wide CRT helpers
 * (wcsstr, wcsncmp, ...). On the host build TCHAR is `char`, and those helpers
 * are provided as narrow inline functions by shim/windows.h. We therefore
 * shadow the real <wchar.h> with this near-empty file so the wchar_t
 * prototypes never get declared and clash with the char based versions.
 */
#ifndef HBX_SHIM_WCHAR_H
#define HBX_SHIM_WCHAR_H

#include <windows.h>

#endif /* HBX_SHIM_WCHAR_H */
