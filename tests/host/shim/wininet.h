/*
 * wininet.h  --  Host build shim
 * ------------------------------
 * Provides exactly the subset of the Windows Internet (WinInet) API that the
 * HBX_USE_WININET code path in HttpClient.cpp touches, so that path can be
 * syntax-checked on a POSIX host with g++ (e.g. `-DHBX_USE_WININET
 * -fsyntax-only`). The real device build uses the genuine <wininet.h> from the
 * Windows Mobile 6.5 SDK; this shim is never seen there.
 *
 * The stubs return benign values and perform no networking -- host tests only
 * exercise the pure ParseUrl helper, never SendRequestWinInet. All new symbols
 * live here so shim/windows.h stays untouched.
 */
#ifndef HBX_SHIM_WININET_H
#define HBX_SHIM_WININET_H

#include <windows.h>

/* ---------------------------------------------------------------- types --- */
typedef void*     HINTERNET;
typedef WORD      INTERNET_PORT;
#ifndef DWORD_PTR
typedef uintptr_t DWORD_PTR;
#endif

/* -------------------------------------------------------------- constants -- */
/* InternetOpen dwAccessType */
#define INTERNET_OPEN_TYPE_DIRECT   1
/* InternetConnect dwService */
#define INTERNET_SERVICE_HTTP       3
/* HttpOpenRequest dwFlags */
#define INTERNET_FLAG_SECURE        0x00800000
#define INTERNET_FLAG_RELOAD        0x80000000
/* HttpAddRequestHeaders dwModifiers */
#define HTTP_ADDREQ_FLAG_ADD        0x20000000
/* HttpQueryInfo dwInfoLevel */
#define HTTP_QUERY_STATUS_CODE      19
#define HTTP_QUERY_FLAG_NUMBER      0x20000000

/* -------------------------------------------------------------- functions -- */
inline HINTERNET InternetOpen(LPCTSTR /*agent*/, DWORD /*accessType*/,
                              LPCTSTR /*proxy*/, LPCTSTR /*proxyBypass*/,
                              DWORD /*flags*/)
{
    return (HINTERNET)(intptr_t)0x1;
}

inline HINTERNET InternetConnect(HINTERNET /*session*/, LPCTSTR /*server*/,
                                 INTERNET_PORT /*port*/, LPCTSTR /*user*/,
                                 LPCTSTR /*password*/, DWORD /*service*/,
                                 DWORD /*flags*/, DWORD_PTR /*context*/)
{
    return (HINTERNET)(intptr_t)0x2;
}

inline HINTERNET HttpOpenRequest(HINTERNET /*connect*/, LPCTSTR /*verb*/,
                                 LPCTSTR /*objectName*/, LPCTSTR /*version*/,
                                 LPCTSTR /*referrer*/, LPCTSTR* /*acceptTypes*/,
                                 DWORD /*flags*/, DWORD_PTR /*context*/)
{
    return (HINTERNET)(intptr_t)0x3;
}

inline BOOL HttpAddRequestHeaders(HINTERNET /*request*/, LPCTSTR /*headers*/,
                                  DWORD /*headersLength*/, DWORD /*modifiers*/)
{
    return TRUE;
}

inline BOOL HttpSendRequest(HINTERNET /*request*/, LPCTSTR /*headers*/,
                            DWORD /*headersLength*/, LPVOID /*optional*/,
                            DWORD /*optionalLength*/)
{
    return TRUE;
}

inline BOOL HttpQueryInfo(HINTERNET /*request*/, DWORD /*infoLevel*/,
                          LPVOID buffer, LPDWORD bufferLength, LPDWORD /*index*/)
{
    /* Report a benign 200 if the caller passed a DWORD-sized buffer. */
    if (buffer && bufferLength && *bufferLength >= sizeof(DWORD)) {
        *(DWORD*)buffer = 200;
    }
    return TRUE;
}

inline BOOL InternetReadFile(HINTERNET /*file*/, LPVOID /*buffer*/,
                             DWORD /*bytesToRead*/, LPDWORD bytesRead)
{
    /* No data on the host: report zero bytes so the read loop terminates. */
    if (bytesRead) *bytesRead = 0;
    return TRUE;
}

inline BOOL InternetCloseHandle(HINTERNET /*handle*/)
{
    return TRUE;
}

#endif /* HBX_SHIM_WININET_H */
