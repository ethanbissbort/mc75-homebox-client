/*
 * winsock.h  --  Host build shim
 * ------------------------------
 * Maps the small WinSock surface used by HttpClient onto the POSIX BSD socket
 * API so HttpClient.cpp compiles (and, if a server is reachable, could even
 * run) on the host. The unit tests only exercise the pure ParseUrl helper, so
 * no socket is actually opened during testing.
 */
#ifndef HBX_SHIM_WINSOCK_H
#define HBX_SHIM_WINSOCK_H

#include <windows.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

typedef int SOCKET;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR   (-1)
#endif

typedef struct _WSADATA {
    WORD wVersion;
} WSADATA;

#define MAKEWORD(low, high) \
    ((WORD)(((BYTE)((low) & 0xff)) | (((WORD)((BYTE)((high) & 0xff))) << 8)))

inline int WSAStartup(WORD /*versionRequested*/, WSADATA* /*data*/) { return 0; }
inline int WSACleanup() { return 0; }
inline int closesocket(SOCKET s) { return ::close(s); }

#endif /* HBX_SHIM_WINSOCK_H */
