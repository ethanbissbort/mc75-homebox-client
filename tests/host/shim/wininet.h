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
/* Suppresses the cookie jar: NetBox's session auth is listed ahead of token
 * auth, so a replayed sessionid cookie turns on CSRF checking and every write
 * fails 403. */
#define INTERNET_FLAG_NO_COOKIES    0x00080000
/* NetBox answers 301 for a URL missing its trailing slash; following that would
 * re-issue a PATCH as a GET. */
#define INTERNET_FLAG_NO_AUTO_REDIRECT 0x00200000
/* No modal dialogs -- requests run on the sync thread. */
#define INTERNET_FLAG_NO_UI         0x00000200
/* Opt-in certificate relaxation (HttpClient::SetIgnoreCertificateErrors). */
#define INTERNET_FLAG_IGNORE_CERT_CN_INVALID   0x00001000
#define INTERNET_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
/* INTERNET_OPTION_SECURITY_FLAGS bits. An untrusted CA has no HttpOpenRequest
 * flag; it can only be waived here. */
#define SECURITY_FLAG_IGNORE_UNKNOWN_CA        0x00000100
#define SECURITY_FLAG_IGNORE_WRONG_USAGE       0x00000200
#define SECURITY_FLAG_IGNORE_CERT_CN_INVALID   0x00001000
#define SECURITY_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
/* HttpAddRequestHeaders dwModifiers */
#define HTTP_ADDREQ_FLAG_ADD        0x20000000
/* HttpQueryInfo dwInfoLevel */
#define HTTP_QUERY_STATUS_CODE      19
#define HTTP_QUERY_FLAG_NUMBER      0x20000000
/* InternetSetOption dwOption -- values match the Windows Mobile 6.5 SDK. */
#define INTERNET_OPTION_CONNECT_TIMEOUT 2
#define INTERNET_OPTION_SEND_TIMEOUT    5
#define INTERNET_OPTION_RECEIVE_TIMEOUT 6
#define INTERNET_OPTION_SECURITY_FLAGS  31

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

inline BOOL InternetSetOption(HINTERNET /*handle*/, DWORD /*option*/,
                              LPVOID /*buffer*/, DWORD /*bufferLength*/)
{
    return TRUE;
}

inline BOOL InternetQueryOption(HINTERNET /*handle*/, DWORD /*option*/,
                                LPVOID buffer, LPDWORD bufferLength)
{
    /* Report an empty flag set if the caller passed a DWORD-sized buffer. */
    if (buffer && bufferLength && *bufferLength >= sizeof(DWORD)) {
        *(DWORD*)buffer = 0;
    }
    return TRUE;
}

inline BOOL InternetCloseHandle(HINTERNET /*handle*/)
{
    return TRUE;
}

#endif /* HBX_SHIM_WININET_H */
