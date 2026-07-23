/*
 * aygshell.h  --  Host build shim
 * -------------------------------
 * Minimal Windows Mobile shell surface (the soft-key menu bar) used by
 * Controller to host a navigation menu. Inert stubs so the controller
 * translation unit compiles on the host build gate; the real aygshell.h /
 * aygshell.lib are used on-device.
 */
#ifndef HBX_SHIM_AYGSHELL_H
#define HBX_SHIM_AYGSHELL_H

#include <windows.h>

/* Menu-bar creation flags */
#define SHCMBF_HIDDEN        0x0001
#define SHCMBF_HIDESIPBUTTON 0x0004
#define SHCMBF_EMPTYBAR      0x0008

typedef struct tagSHMENUBARINFO {
    DWORD     cbSize;
    HWND      hwndParent;
    DWORD     dwFlags;
    UINT      nToolBarId;
    HINSTANCE hInstRes;
    int       nBmpId;
    int       cBmpImages;
    HWND      hwndMB;
    DWORD     dwReserved;   /* COLORREF in the real header; unused here */
} SHMENUBARINFO;

inline BOOL SHCreateMenuBar(SHMENUBARINFO* pmb)
{
    if (pmb) pmb->hwndMB = (HWND)0;
    return TRUE;
}

/* Full-screen helper (declared for completeness; unused by the core flow). */
#define SHFS_HIDETASKBAR   0x0002
#define SHFS_HIDESIPBUTTON 0x0008
#define SHFS_HIDESTARTICON 0x0010
inline BOOL SHFullScreen(HWND /*hwnd*/, DWORD /*state*/) { return TRUE; }

#endif /* HBX_SHIM_AYGSHELL_H */
