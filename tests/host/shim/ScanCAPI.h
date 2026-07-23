/*
 * ScanCAPI.h  --  HOST BUILD SHIM for the Symbol / Zebra Scanner C API
 * -------------------------------------------------------------------------
 * This is NOT the real EMDK / SMDK "ScanCAPI.h". It is a tiny, self-consistent
 * stand-in that declares EXACTLY the subset of the Symbol Scanner C API that
 * ScannerHAL.cpp touches when it is compiled with -DHBX_USE_EMDK. Its only job
 * is to let the *real* (device) code path be COMPILE-CHECKED on a POSIX host
 * with g++, alongside the rest of the test build.
 *
 * On the actual Motorola MC75 / Windows Mobile 6.5 device build:
 *   - HBX_USE_EMDK is defined,
 *   - the include path points at the real Zebra EMDK "Scanner C API" headers,
 *     so <ScanCAPI.h> resolves to the vendor header (NOT this file), and
 *   - ScanAPIWM.lib / ScanAPI.lib provides the real SCAN_* symbols.
 * This shim is only ever found on the host test include path, so nothing here
 * can leak into or clash with the on-device build.
 *
 * The function bodies below are inert no-op stubs that just return
 * E_SCN_SUCCESS (or a valid buffer). They exist so the linker/compiler is
 * satisfied on the host; they never drive real hardware.
 *
 * Sources used to model the real surface (naming/behaviour):
 *   - Tek-Tips "barcode scanning" thread (SCAN_Open/Enable/AllocateBuffer/
 *     ReadLabelWait/ReadLabelMsg/DeallocateBuffer/Close, "SCN1:" port name).
 *   - rhomobile/rhodes wm Scanner.cpp (SCNBUF_GET* accessor macros,
 *     SCAN_SetSoftTrigger, E_SCN_SUCCESS checks).
 *   - Zebra TechDocs / EMDK "Scanner C API" reference.
 * Where a field/constant value is not published it is given a plausible value
 * and flagged with an ASSUMPTION comment; the real header supplies the true
 * values on-device. Only symbol *names* and *signatures* need to agree with the
 * vendor header for the device build to compile and link, and those are the
 * documented ones.
 */
#ifndef HBX_SHIM_SCANCAPI_H
#define HBX_SHIM_SCANCAPI_H

#include <windows.h>   /* host shim: TCHAR==char, HANDLE, DWORD, HWND, UINT ... */

/* ===================================================================== *
 *  Return / status codes
 * ===================================================================== */
/* E_SCN_SUCCESS is 0 in the real API; a non-zero code means failure.       */
#ifndef E_SCN_SUCCESS
#define E_SCN_SUCCESS        ((DWORD)0)
#endif
/* Returned by a timed read when no label was decoded before the timeout.   */
/* ASSUMPTION: exact numeric value is vendor-defined; only the name matters. */
#ifndef E_SCN_READTIMEOUT
#define E_SCN_READTIMEOUT    ((DWORD)0x00000102)
#endif
#ifndef E_SCN_DEVICEFAILURE
#define E_SCN_DEVICEFAILURE  ((DWORD)0x00000001)
#endif

/* ===================================================================== *
 *  Trigger modes (SCAN_PARAMS.dwTriggerMode)
 * ===================================================================== */
/* TRIG_MODE_LEVEL   : continuous / level trigger (0 == continuous mode).   */
/* TRIG_MODE_ONESHOT : single decode per trigger pull (1 == single mode).   */
#ifndef TRIG_MODE_LEVEL
#define TRIG_MODE_LEVEL      ((DWORD)0)
#endif
#ifndef TRIG_MODE_ONESHOT
#define TRIG_MODE_ONESHOT    ((DWORD)1)
#endif

/* ===================================================================== *
 *  Handle type
 * ===================================================================== */
/* The real API uses a plain HANDLE for the opened scanner; SCAN_HANDLE is a
 * convenience alias so cast sites read naturally. ScannerHAL keeps its member
 * as a HANDLE and casts where a SCAN_HANDLE is spelled out. */
typedef HANDLE SCAN_HANDLE;

/* ===================================================================== *
 *  SCAN_BUFFER  --  holds one decoded label
 * ===================================================================== */
/* Real code accesses the decoded bytes / length / symbology through the
 * SCNBUF_* accessor macros rather than the raw struct fields, so those macros
 * are what ScannerHAL.cpp uses. The field layout here is a plausible stand-in
 * (ASSUMPTION); the vendor header defines the true layout, but the macros
 * insulate the caller from it. 7095 is the documented maximum label size. */
#ifndef SCAN_MAX_LABEL_LEN
#define SCAN_MAX_LABEL_LEN   7095
#endif

typedef struct _SCAN_BUFFER {
    DWORD dwStructSize;                 /* size of this struct               */
    DWORD dwBytesRead;                  /* length of decoded data, in bytes  */
    DWORD dwStatus;                     /* decode status                     */
    DWORD dwScanType;                   /* symbology / label type            */
    BYTE  szData[SCAN_MAX_LABEL_LEN];   /* decoded data bytes                */
} SCAN_BUFFER, *LPSCAN_BUFFER;

/* Accessor macros (mirror the real ScanCAPI SCNBUF_* macro family). */
#define SCNBUF_GETDATA(p)    ((BYTE*)((LPSCAN_BUFFER)(p))->szData)
#define SCNBUF_GETLEN(p)     (((LPSCAN_BUFFER)(p))->dwBytesRead)
#define SCNBUF_GETLBLTYP(p)  (((LPSCAN_BUFFER)(p))->dwScanType)
#define SCNBUF_GETSTAT(p)    (((LPSCAN_BUFFER)(p))->dwStatus)

/* ===================================================================== *
 *  SCAN_PARAMS  --  scanner configuration
 * ===================================================================== */
/* Field names follow the ones referenced by the original ScannerHAL stub
 * comments. ASSUMPTION: the real SCAN_PARAMS is larger and its exact field
 * names may differ between EMDK versions; ScannerHAL only touches the trigger,
 * beep and vibrate members named here. */
typedef struct _SCAN_PARAMS {
    DWORD dwStructSize;
    DWORD dwTriggerMode;            /* TRIG_MODE_LEVEL / TRIG_MODE_ONESHOT   */
    DWORD dwDecodeBeepEnable;       /* 0/1                                    */
    DWORD dwDecodeBeepTime;         /* ms                                     */
    DWORD dwDecodeBeepFrequency;    /* Hz                                     */
    DWORD dwDecodeVibrateEnable;    /* 0/1                                    */
    DWORD dwDecodeVibrateTime;      /* ms                                     */
} SCAN_PARAMS, *LPSCAN_PARAMS;

/* ===================================================================== *
 *  Function prototypes  (inert host stubs)
 * ===================================================================== */
/* Lifecycle ----------------------------------------------------------- */
inline DWORD SCAN_Open(LPCTSTR /*pszScannerName*/, HANDLE* phScanner)
{
    if (phScanner) *phScanner = (HANDLE)0x1; /* non-NULL so open "succeeds" */
    return E_SCN_SUCCESS;
}
inline DWORD SCAN_Close(HANDLE /*hScanner*/)            { return E_SCN_SUCCESS; }
inline DWORD SCAN_Enable(HANDLE /*hScanner*/)           { return E_SCN_SUCCESS; }
inline DWORD SCAN_Disable(HANDLE /*hScanner*/)          { return E_SCN_SUCCESS; }
inline DWORD SCAN_Flush(HANDLE /*hScanner*/)            { return E_SCN_SUCCESS; }

/* Programmatic ("soft") trigger pull. bTriggerFlag: FALSE releases,
 * TRUE pulls the trigger and fires the beam. */
inline DWORD SCAN_SetSoftTrigger(HANDLE /*hScanner*/, BOOL /*bTriggerFlag*/)
{
    return E_SCN_SUCCESS;
}

/* Buffer management --------------------------------------------------- */
inline LPSCAN_BUFFER SCAN_AllocateBuffer(BOOL /*bText*/, DWORD /*dwBufSize*/)
{
    static SCAN_BUFFER s_buf;      /* stub: single reusable static buffer */
    s_buf.dwStructSize = (DWORD)sizeof(SCAN_BUFFER);
    s_buf.dwBytesRead  = 0;
    return &s_buf;
}
inline DWORD SCAN_DeallocateBuffer(LPSCAN_BUFFER /*lpScanBuffer*/)
{
    return E_SCN_SUCCESS;
}

/* Reads --------------------------------------------------------------- */
/* Blocking read: returns when a label is decoded or dwTimeout (ms) elapses.
 * This is the call the ScannerHAL scan thread polls with. */
inline DWORD SCAN_ReadLabelWait(HANDLE /*hScanner*/, LPSCAN_BUFFER /*lpScanBuffer*/,
                                DWORD /*dwTimeout*/)
{
    return E_SCN_READTIMEOUT;     /* stub: never decodes anything on host */
}
/* Asynchronous read: arms the beam and posts uiMsgNo to hWnd on completion.
 * Declared for completeness / parity with the vendor API; the polling design
 * uses SCAN_ReadLabelWait instead (see ScannerHAL.cpp ScanThread). */
inline DWORD SCAN_ReadLabelMsg(HANDLE /*hScanner*/, LPSCAN_BUFFER /*lpScanBuffer*/,
                               HWND /*hWnd*/, UINT /*uiMsgNo*/, DWORD /*dwTimeout*/,
                               LPDWORD lpdwRequestID)
{
    if (lpdwRequestID) *lpdwRequestID = 0;
    return E_SCN_SUCCESS;
}

/* Configuration ------------------------------------------------------- */
inline DWORD SCAN_GetParameters(HANDLE /*hScanner*/, LPSCAN_PARAMS lpParams)
{
    if (lpParams) {
        lpParams->dwStructSize          = (DWORD)sizeof(SCAN_PARAMS);
        lpParams->dwTriggerMode         = TRIG_MODE_LEVEL;
        lpParams->dwDecodeBeepEnable    = 1;
        lpParams->dwDecodeBeepTime      = 200;
        lpParams->dwDecodeBeepFrequency = 2500;
        lpParams->dwDecodeVibrateEnable = 0;
        lpParams->dwDecodeVibrateTime   = 200;
    }
    return E_SCN_SUCCESS;
}
inline DWORD SCAN_SetParameters(HANDLE /*hScanner*/, LPSCAN_PARAMS /*lpParams*/)
{
    return E_SCN_SUCCESS;
}

/* ===================================================================== *
 *  Host-only Win32 helpers the real code path needs
 * ---------------------------------------------------------------------
 *  MultiByteToWideChar / CP_ACP are provided by the real <windows.h> on the
 *  device, but the host windows.h shim does not declare them (and we must not
 *  edit that file). They are declared here so the -DHBX_USE_EMDK code path
 *  compiles on the host. On the host TCHAR==char, so this is a bounded byte
 *  copy; on-device the genuine wide-conversion API is used instead and this
 *  shim is never seen.
 * ===================================================================== */
#ifndef CP_ACP
#define CP_ACP  ((UINT)0)
#endif

/* Destination is TCHAR* (== char* on the host) to match the real device
 * signature's LPWSTR (== TCHAR* == WCHAR*) at the ScannerHAL call site. */
inline int MultiByteToWideChar(UINT /*CodePage*/, DWORD /*dwFlags*/,
                               const char* lpMultiByteStr, int cbMultiByte,
                               TCHAR* lpWideCharStr, int cchWideChar)
{
    if (!lpWideCharStr || cchWideChar <= 0) return 0;
    int written = 0;
    if (cbMultiByte < 0) {
        /* NUL-terminated source: copy including the terminator. */
        while (lpMultiByteStr && lpMultiByteStr[written] &&
               written < cchWideChar - 1) {
            lpWideCharStr[written] = (TCHAR)lpMultiByteStr[written];
            ++written;
        }
        lpWideCharStr[written] = (TCHAR)0;
        return written + 1;
    }
    /* Explicit length: copy exactly cbMultiByte bytes (no terminator added). */
    {
        int n = (cbMultiByte < cchWideChar) ? cbMultiByte : cchWideChar;
        int i = 0;
        for (; i < n; ++i) {
            lpWideCharStr[i] = (TCHAR)(lpMultiByteStr ? lpMultiByteStr[i] : 0);
        }
        written = i;
    }
    return written;
}

#endif /* HBX_SHIM_SCANCAPI_H */
