#ifndef RESOURCE_H
#define RESOURCE_H

// Application icons
#define IDI_APPICON                     100

// Windows Mobile soft-key menu bar + the popup menu it hosts.
// SHCreateMenuBar is given IDR_MENUBAR; the first field of that SHMENUBAR
// template names IDM_MAINMENU, the MENU resource the popups come from.
#define IDR_MENUBAR                     101
#define IDM_MAINMENU                    102

// Menu commands
#define IDM_FILE_EXIT                   1001
#define IDM_HELP_ABOUT                  1002
#define IDM_VIEW_SCAN                   1010
#define IDM_VIEW_QUEUE                  1011
#define IDM_ACTION_SYNC                 1012
#define IDM_VIEW_ITEM                   1013
#define IDM_ACTION_SETLOC               1014

// Chooses which configured inventory system takes new work. Handled in
// Controller::WindowProc; see also IDM_MAINMENU in layout.rc.
#define IDM_ACTION_SWITCHBACKEND        1015

// Control IDs
#define IDC_SCAN_BUTTON                 2001
#define IDC_SYNC_BUTTON                 2002
#define IDC_QUEUE_BUTTON                2003

// String resources
#define IDS_APP_TITLE                   3001
#define IDS_APP_VERSION                 3002
#define IDS_MENU                        3003

#endif // RESOURCE_H
