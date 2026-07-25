/*
 * windows.h  --  Host build shim for the MC75 HomeBox client
 * -----------------------------------------------------------
 * This header lets the platform-independent CORE LOGIC of the Windows Mobile
 * 6.5 application (JsonLite, Item, Location, Journal, Config, HttpClient URL
 * parsing, HbClient request-shaping) compile and run natively under g++ on a
 * POSIX host, so the unit / integration tests can execute in CI without a
 * device or the Windows Mobile SDK.
 *
 * It is NOT a general purpose Win32 emulator. It provides exactly the subset
 * of types, macros and functions the core files touch:
 *   - TCHAR is a plain `char` (TEXT("x") therefore expands to a narrow "x"),
 *     which makes wsprintf's "%s" semantics match Win32's wsprintfW.
 *   - The Windows string helpers (lstr*, wcs*) are implemented as inline
 *     functions over <cstring>/<cstdlib>. Because the real wide versions
 *     (wchar_t based) are never pulled in on the host, there is no clash.
 *   - The file I/O layer (CreateFile/ReadFile/... ) is mapped onto POSIX
 *     open/read/write/lseek so Journal and Config exercise real files.
 *
 * The real Windows Mobile build never sees this file (it is only on the host
 * test include path).
 */
#ifndef HBX_SHIM_WINDOWS_H
#define HBX_SHIM_WINDOWS_H

#include <stdint.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ types */
typedef char            TCHAR;
typedef char            CHAR;
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;
typedef int             BOOL;
typedef void*           HANDLE;
typedef void*           LPVOID;
typedef const void*     LPCVOID;
typedef const char*     LPCTSTR;
typedef char*           LPTSTR;
typedef unsigned int    UINT;
typedef long            LONG;

/* ---------------------------------------------------------------- macros  */
#ifndef NULL
#define NULL 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* On the host build TCHAR == char, so string literals stay narrow. */
#define TEXT(quote) quote
#define _T(quote)   quote

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define INVALID_HANDLE_VALUE     ((HANDLE)(intptr_t)-1)

/* CreateFile desired-access flags */
#define GENERIC_READ             0x80000000u
#define GENERIC_WRITE            0x40000000u

/* CreateFile share flags */
#define FILE_SHARE_READ          0x00000001u
#define FILE_SHARE_WRITE         0x00000002u

/* CreateFile creation disposition */
#define CREATE_NEW               1
#define CREATE_ALWAYS            2
#define OPEN_EXISTING            3
#define OPEN_ALWAYS              4
#define TRUNCATE_EXISTING        5

#define FILE_ATTRIBUTE_NORMAL    0x00000080u

/* SetFilePointer move method */
#define FILE_BEGIN               0
#define FILE_CURRENT             1
#define FILE_END                 2

#define INVALID_FILE_SIZE        0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu

/* ---------------------------------------------- Windows string functions  */
/* Names below do not exist in the C library, so no clash on the host. */
inline int   lstrlen(const TCHAR* s)                 { return s ? (int)std::strlen(s) : 0; }
inline TCHAR* lstrcpy(TCHAR* d, const TCHAR* s)      { return std::strcpy(d, s); }
inline TCHAR* lstrcpyn(TCHAR* d, const TCHAR* s, int n)
{
    if (n <= 0) return d;
    std::strncpy(d, s, (size_t)(n - 1));
    d[n - 1] = '\0';
    return d;
}
inline TCHAR* lstrcat(TCHAR* d, const TCHAR* s)      { return std::strcat(d, s); }
inline int   lstrcmp(const TCHAR* a, const TCHAR* b) { return std::strcmp(a, b); }
inline int   lstrcmpi(const TCHAR* a, const TCHAR* b){ return std::strcmp(a, b); }

/*
 * The core code refers to the "wide" CRT helpers by their wchar names. On the
 * host TCHAR is char, so these forward to the narrow equivalents. The real
 * <wchar.h> is shimmed away (see shim/wchar.h) so these never collide with the
 * wchar_t prototypes from the C library.
 */
inline const TCHAR* wcsstr(const TCHAR* hay, const TCHAR* needle) { return std::strstr(hay, needle); }
inline const TCHAR* wcschr(const TCHAR* s, int c)                 { return std::strchr(s, c); }
inline int   wcsncmp(const TCHAR* a, const TCHAR* b, size_t n)    { return std::strncmp(a, b, n); }
inline int   wcscmp(const TCHAR* a, const TCHAR* b)              { return std::strcmp(a, b); }
inline size_t wcslen(const TCHAR* s)                            { return std::strlen(s); }
inline TCHAR* wcsncpy(TCHAR* d, const TCHAR* s, size_t n)       { return std::strncpy(d, s, n); }
inline int   _wtoi(const TCHAR* s)                              { return std::atoi(s); }
inline long  wcstol(const TCHAR* s, TCHAR** end, int base)      { return std::strtol(s, end, base); }

/* wsprintf: Win32 has no size argument, but wsprintfW *does* stop after 1024
 * characters including the NUL. Reproducing that cap here matters: without it
 * the host build would happily write past a length the device silently
 * truncates, so a buffer defect would present completely differently on the
 * two targets. vsnprintf with the same limit gives identical semantics. */
#define HBX_WSPRINTF_MAX 1024
inline int wsprintf(TCHAR* buffer, const TCHAR* format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(buffer, HBX_WSPRINTF_MAX, format, args);
    va_end(args);
    /* Win32 returns the number of characters actually written. */
    if (n < 0) {
        buffer[0] = 0;
        return 0;
    }
    if (n >= HBX_WSPRINTF_MAX) {
        n = HBX_WSPRINTF_MAX - 1;
    }
    return n;
}

/* ------------------------------------------------------ time / SYSTEMTIME */
typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

inline void GetLocalTime(SYSTEMTIME* st)
{
    if (!st) return;
    time_t t = ::time(NULL);
    struct tm lt;
    ::localtime_r(&t, &lt);
    st->wYear         = (WORD)(1900 + lt.tm_year);
    st->wMonth        = (WORD)(lt.tm_mon + 1);
    st->wDayOfWeek    = (WORD)lt.tm_wday;
    st->wDay          = (WORD)lt.tm_mday;
    st->wHour         = (WORD)lt.tm_hour;
    st->wMinute       = (WORD)lt.tm_min;
    st->wSecond       = (WORD)lt.tm_sec;
    st->wMilliseconds = 0;
}

inline DWORD GetTickCount()
{
    return (DWORD)(((unsigned long long)::clock() * 1000ULL) / CLOCKS_PER_SEC);
}

inline void Sleep(DWORD ms) { (void)ms; }

/* ------------------------------------------------------- file I/O (POSIX) */
inline HANDLE CreateFile(const TCHAR* path, DWORD access, DWORD /*share*/,
                         LPVOID /*sec*/, DWORD disposition, DWORD /*flags*/,
                         HANDLE /*templ*/)
{
    int oflag;
    bool wantWrite = (access & GENERIC_WRITE) != 0;
    /* Open writable handles read/write so seek-then-read after write works. */
    oflag = wantWrite ? O_RDWR : O_RDONLY;

    switch (disposition) {
        case CREATE_NEW:        oflag |= O_CREAT | O_EXCL;  break;
        case CREATE_ALWAYS:     oflag |= O_CREAT | O_TRUNC; break;
        case OPEN_ALWAYS:       oflag |= O_CREAT;           break;
        case TRUNCATE_EXISTING: oflag |= O_TRUNC;           break;
        case OPEN_EXISTING:     default:                    break;
    }

    int fd = ::open(path, oflag, 0644);
    if (fd < 0) {
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)(intptr_t)fd;
}

inline BOOL ReadFile(HANDLE h, LPVOID buffer, DWORD toRead, DWORD* bytesRead, LPVOID /*ov*/)
{
    int fd = (int)(intptr_t)h;
    ssize_t r = ::read(fd, buffer, toRead);
    if (r < 0) {
        if (bytesRead) *bytesRead = 0;
        return FALSE;
    }
    if (bytesRead) *bytesRead = (DWORD)r;
    return TRUE;
}

inline BOOL WriteFile(HANDLE h, LPCVOID buffer, DWORD toWrite, DWORD* bytesWritten, LPVOID /*ov*/)
{
    int fd = (int)(intptr_t)h;
    ssize_t w = ::write(fd, buffer, toWrite);
    if (w < 0) {
        if (bytesWritten) *bytesWritten = 0;
        return FALSE;
    }
    if (bytesWritten) *bytesWritten = (DWORD)w;
    return TRUE;
}

inline DWORD SetFilePointer(HANDLE h, LONG dist, LONG* /*distHigh*/, DWORD method)
{
    int fd = (int)(intptr_t)h;
    int whence = (method == FILE_BEGIN) ? SEEK_SET
               : (method == FILE_END)   ? SEEK_END
                                        : SEEK_CUR;
    off_t r = ::lseek(fd, dist, whence);
    if (r < 0) return INVALID_SET_FILE_POINTER;
    return (DWORD)r;
}

inline DWORD GetFileSize(HANDLE h, DWORD* sizeHigh)
{
    int fd = (int)(intptr_t)h;
    struct stat st;
    if (::fstat(fd, &st) != 0) return INVALID_FILE_SIZE;
    if (sizeHigh) *sizeHigh = 0;
    return (DWORD)st.st_size;
}

inline BOOL CloseHandle(HANDLE h)
{
    int fd = (int)(intptr_t)h;
    if (fd >= 0) ::close(fd);
    return TRUE;
}

inline BOOL DeleteFile(const TCHAR* path)          { return ::unlink(path) == 0 ? TRUE : FALSE; }
inline BOOL MoveFile(const TCHAR* from, const TCHAR* to) { return ::rename(from, to) == 0 ? TRUE : FALSE; }
inline BOOL FlushFileBuffers(HANDLE h)             { int fd = (int)(intptr_t)h; return ::fsync(fd) == 0 ? TRUE : FALSE; }

typedef DWORD (*LPTHREAD_START_ROUTINE)(void*);
inline HANDLE CreateThread(void* /*sec*/, DWORD /*stack*/, LPTHREAD_START_ROUTINE /*start*/,
                           void* /*param*/, DWORD /*flags*/, DWORD* /*idOut*/)
{
    // No real thread on the host; the scanner monitor loop is device-only.
    return (HANDLE)(intptr_t)0x1000;
}
inline DWORD WaitForSingleObject(HANDLE /*h*/, DWORD /*ms*/) { return 0; }

/* ------------------------------------------------ critical sections (real)
 * Backed by a real recursive pthread mutex rather than a no-op. The device
 * code takes these locks on the EMDK scanner thread and the UI thread, and a
 * no-op here would let a recursive-acquire or unbalanced-release bug compile
 * and "pass" on the host while deadlocking on the MC75. */
typedef struct _CRITICAL_SECTION {
    pthread_mutex_t mutex;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

inline void InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    pthread_mutexattr_t attr;
    ::pthread_mutexattr_init(&attr);
    ::pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    ::pthread_mutex_init(&cs->mutex, &attr);
    ::pthread_mutexattr_destroy(&attr);
}
inline void DeleteCriticalSection(LPCRITICAL_SECTION cs)  { ::pthread_mutex_destroy(&cs->mutex); }
inline void EnterCriticalSection(LPCRITICAL_SECTION cs)   { ::pthread_mutex_lock(&cs->mutex); }
inline void LeaveCriticalSection(LPCRITICAL_SECTION cs)   { ::pthread_mutex_unlock(&cs->mutex); }

/* ---------------------------------------------------------- interlocked ops
 * GCC's __sync builtins give the same full-barrier semantics WinCE provides. */
inline LONG InterlockedIncrement(LONG volatile* target) { return __sync_add_and_fetch(target, 1); }
inline LONG InterlockedDecrement(LONG volatile* target) { return __sync_sub_and_fetch(target, 1); }
inline LONG InterlockedExchange(LONG volatile* target, LONG value)
{
    return __sync_lock_test_and_set(target, value);
}

/* ============================================================= GUI shim ===
 * The types/constants/functions below let the Win32/WinCE GUI translation
 * units (Controller, main, ScannerHAL, the Views) COMPILE on the host so the
 * whole codebase can be checked with one compiler. They are inert stubs -
 * the tests never construct windows - but they let the build gate catch any
 * real compile error in the UI layer.
 * ========================================================================= */

typedef uintptr_t   UINT_PTR;
typedef intptr_t    LONG_PTR;
typedef UINT_PTR    WPARAM;
typedef LONG_PTR    LPARAM;
typedef LONG_PTR    LRESULT;
typedef unsigned short ATOM;
typedef wchar_t*    LPWSTR;
typedef DWORD*      LPDWORD;

typedef void* HWND;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HMENU;
typedef void* HBRUSH;
typedef void* HFONT;
typedef void* HGDIOBJ;
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HDC;

#define WINAPI
#define CALLBACK
#define APIENTRY

typedef LRESULT (CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct _RECT  { LONG left; LONG top; LONG right; LONG bottom; } RECT;
typedef struct _POINT { LONG x; LONG y; } POINT;

typedef struct _MSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG;

typedef struct _WNDCLASS {
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCTSTR   lpszMenuName;
    LPCTSTR   lpszClassName;
} WNDCLASS;

typedef struct _CREATESTRUCT {
    LPVOID    lpCreateParams;
    HINSTANCE hInstance;
    HMENU     hMenu;
    HWND      hwndParent;
    int       cy;
    int       cx;
    int       y;
    int       x;
    LONG      style;
    LPCTSTR   lpszName;
    LPCTSTR   lpszClass;
    DWORD     dwExStyle;
} CREATESTRUCT;

#define LF_FACESIZE 32
typedef struct _LOGFONT {
    LONG  lfHeight;
    LONG  lfWidth;
    LONG  lfEscapement;
    LONG  lfOrientation;
    LONG  lfWeight;
    BYTE  lfItalic;
    BYTE  lfUnderline;
    BYTE  lfStrikeOut;
    BYTE  lfCharSet;
    BYTE  lfOutPrecision;
    BYTE  lfClipPrecision;
    BYTE  lfQuality;
    BYTE  lfPitchAndFamily;
    TCHAR lfFaceName[LF_FACESIZE];
} LOGFONT;

/* Window styles */
#define WS_OVERLAPPED   0x00000000u
#define WS_CHILD        0x40000000u
#define WS_VISIBLE      0x10000000u
#define WS_BORDER       0x00800000u
/* Static / edit / button styles */
#define SS_LEFT         0x00000000u
#define SS_CENTER       0x00000001u
#define ES_LEFT         0x00000000u
#define ES_CENTER       0x00000001u
#define ES_MULTILINE    0x00000004u
#define ES_AUTOVSCROLL  0x00000040u
#define ES_AUTOHSCROLL  0x00000080u
#define ES_READONLY     0x00000800u
#define ES_NUMBER       0x00002000u
#define BS_PUSHBUTTON   0x00000000u
/* Notifications */
#define BN_CLICKED      0
#define EN_CHANGE       0x0300
/* CreateWindow / ShowWindow */
#define CW_USEDEFAULT   ((int)0x80000000)
#define SW_HIDE         0
#define SW_SHOW         5
#define COLOR_WINDOW    5
/* Window messages */
#define WM_CREATE       0x0001
#define WM_DESTROY      0x0002
#define WM_ACTIVATE     0x0006
#define WM_SIZE         0x0005
#define WM_CLOSE        0x0010
#define WM_SETTINGCHANGE 0x001A
#define WM_COMMAND      0x0111
#define WM_TIMER        0x0113
#define WM_NOTIFY       0x004E
#define WM_HIBERNATE    0x03FF
#define WM_APP          0x8000
#define WA_INACTIVE     0
#define EM_LIMITTEXT    0x00C5
#define ERROR_ALREADY_EXISTS 183
/* Get/SetWindowLong indexes */
#define GWL_WNDPROC     (-4)
#define GWL_USERDATA    (-21)
/* MessageBox flags + returns */
#define MB_OK              0x00000000u
#define MB_YESNO           0x00000004u
#define MB_ICONERROR       0x00000010u
#define MB_ICONQUESTION    0x00000020u
#define MB_ICONWARNING     0x00000030u
#define MB_ICONINFORMATION 0x00000040u
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7
/* SetWindowPos flags */
#define SWP_NOSIZE   0x0001
#define SWP_NOMOVE   0x0002
#define SWP_NOZORDER 0x0004
/* GetSystemMetrics + stock objects */
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SYSTEM_FONT 13
#define FW_NORMAL   400
#define FW_BOLD     700

#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xffff))

inline HWND    CreateWindow(LPCTSTR, LPCTSTR, DWORD, int, int, int, int,
                            HWND, HMENU, HINSTANCE, LPVOID) { return (HWND)0; }
inline ATOM    RegisterClass(const WNDCLASS*)            { return (ATOM)1; }
inline LRESULT DefWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL    ShowWindow(HWND, int)                     { return TRUE; }
inline BOOL    UpdateWindow(HWND)                        { return TRUE; }
inline BOOL    DestroyWindow(HWND)                       { return TRUE; }
inline BOOL    InvalidateRect(HWND, const RECT*, BOOL)   { return TRUE; }
inline BOOL    MoveWindow(HWND, int, int, int, int, BOOL){ return TRUE; }
inline BOOL    EnableWindow(HWND, BOOL)                  { return TRUE; }
inline BOOL    GetClientRect(HWND, RECT* r) { if (r) { r->left = r->top = 0; r->right = 240; r->bottom = 320; } return TRUE; }
inline BOOL    GetWindowRect(HWND, RECT* r) { if (r) { r->left = r->top = 0; r->right = 240; r->bottom = 320; } return TRUE; }
inline BOOL    SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
inline BOOL    SetWindowText(HWND, LPCTSTR)              { return TRUE; }
inline int     GetWindowText(HWND, LPTSTR buf, int max)  { if (buf && max > 0) buf[0] = '\0'; return 0; }
inline LONG    SetWindowLong(HWND, int, LONG)            { return 0; }
inline LONG    GetWindowLong(HWND, int)                  { return 0; }
inline int     MessageBox(HWND, LPCTSTR, LPCTSTR, UINT)  { return IDOK; }
inline BOOL    GetMessage(MSG*, HWND, UINT, UINT)        { return FALSE; }
inline BOOL    TranslateMessage(const MSG*)             { return TRUE; }
inline LRESULT DispatchMessage(const MSG*)             { return 0; }
inline void    PostQuitMessage(int)                     {}
inline int     GetSystemMetrics(int)                    { return 240; }
inline HGDIOBJ GetStockObject(int)                      { return (HGDIOBJ)0; }
inline HFONT   CreateFontIndirect(const LOGFONT*)       { return (HFONT)0; }
inline BOOL    DeleteObject(HGDIOBJ)                    { return TRUE; }
inline LRESULT SendMessage(HWND, UINT, WPARAM, LPARAM)  { return 0; }
inline BOOL    PostMessage(HWND, UINT, WPARAM, LPARAM)  { return TRUE; }

/* Timers, the single-instance mutex and the last-error channel. The UI layer
 * uses these for auto-sync, for marshalling a scan off the EMDK thread, and for
 * refusing a second launch; they are inert here but keep that code compiling. */
inline UINT_PTR SetTimer(HWND, UINT_PTR id, UINT, void*) { return id; }
inline BOOL     KillTimer(HWND, UINT_PTR)                { return TRUE; }
inline HANDLE   CreateMutex(void*, BOOL, const TCHAR*)   { return (HANDLE)(intptr_t)0x2000; }
inline BOOL     ReleaseMutex(HANDLE)                     { return TRUE; }
inline DWORD    GetLastError()                           { return 0; }
inline HWND     FindWindow(const TCHAR*, const TCHAR*)   { return (HWND)0; }
inline BOOL     SetForegroundWindow(HWND)                { return TRUE; }

#endif /* HBX_SHIM_WINDOWS_H */
