/*
 * commctrl.h  --  Host build shim
 * -------------------------------
 * Minimal common-controls surface (InitCommonControlsEx + the list-view
 * types/messages/macros) used by Controller and QueueView, so those GUI
 * translation units compile on the host build gate. Inert stubs only.
 */
#ifndef HBX_SHIM_COMMCTRL_H
#define HBX_SHIM_COMMCTRL_H

#include <windows.h>

typedef struct _INITCOMMONCONTROLSEX {
    DWORD dwSize;
    DWORD dwICC;
} INITCOMMONCONTROLSEX;

#define ICC_LISTVIEW_CLASSES 0x00000001
#define ICC_BAR_CLASSES      0x00000004

inline BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX*) { return TRUE; }

#define WC_LISTVIEW TEXT("SysListView32")

/* List-view window styles */
#define LVS_REPORT           0x0001
#define LVS_SINGLESEL        0x0004
/* Extended styles */
#define LVS_EX_GRIDLINES     0x00000001
#define LVS_EX_FULLROWSELECT 0x00000020
/* Item / column masks + states */
#define LVIF_TEXT            0x0001
#define LVCF_WIDTH           0x0002
#define LVCF_TEXT            0x0004
#define LVIS_SELECTED        0x0002
/* Notifications */
#define LVN_FIRST            (0U - 100U)
#define LVN_ITEMCHANGED      (LVN_FIRST - 1)

typedef struct _LVITEM {
    UINT   mask;
    int    iItem;
    int    iSubItem;
    UINT   state;
    UINT   stateMask;
    LPTSTR pszText;
    int    cchTextMax;
    int    iImage;
    LPARAM lParam;
} LVITEM;

typedef struct _LVCOLUMN {
    UINT   mask;
    int    fmt;
    int    cx;
    LPTSTR pszText;
    int    cchTextMax;
    int    iSubItem;
} LVCOLUMN;

typedef struct _NMHDR {
    HWND     hwndFrom;
    UINT_PTR idFrom;
    UINT     code;
} NMHDR;

typedef struct _NMLISTVIEW {
    NMHDR  hdr;
    int    iItem;
    int    iSubItem;
    UINT   uNewState;
    UINT   uOldState;
    UINT   uChanged;
    POINT  ptAction;
    LPARAM lParam;
} NMLISTVIEW;

/* List-view messages */
#define LVM_FIRST                     0x1000
#define LVM_GETITEMCOUNT              (LVM_FIRST + 4)
#define LVM_INSERTITEM                (LVM_FIRST + 7)
#define LVM_DELETEITEM                (LVM_FIRST + 8)
#define LVM_DELETEALLITEMS            (LVM_FIRST + 9)
#define LVM_INSERTCOLUMN              (LVM_FIRST + 27)
#define LVM_SETITEMCOUNT              (LVM_FIRST + 47)
#define LVM_SETEXTENDEDLISTVIEWSTYLE  (LVM_FIRST + 54)

/* Convenience macros (mirror the real <commctrl.h>) */
#define ListView_DeleteAllItems(w) \
    SendMessage((w), LVM_DELETEALLITEMS, 0, 0)
#define ListView_DeleteItem(w, i) \
    SendMessage((w), LVM_DELETEITEM, (WPARAM)(int)(i), 0)
#define ListView_InsertItem(w, p) \
    ((int)SendMessage((w), LVM_INSERTITEM, 0, (LPARAM)(const LVITEM*)(p)))
#define ListView_GetItemCount(w) \
    ((int)SendMessage((w), LVM_GETITEMCOUNT, 0, 0))
#define ListView_SetItemCount(w, c) \
    SendMessage((w), LVM_SETITEMCOUNT, (WPARAM)(int)(c), 0)
#define ListView_InsertColumn(w, i, p) \
    ((int)SendMessage((w), LVM_INSERTCOLUMN, (WPARAM)(int)(i), (LPARAM)(const LVCOLUMN*)(p)))
#define ListView_SetExtendedListViewStyle(w, s) \
    SendMessage((w), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (LPARAM)(s))

#endif /* HBX_SHIM_COMMCTRL_H */
