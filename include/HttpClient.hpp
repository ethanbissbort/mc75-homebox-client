#ifndef HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#include <windows.h>
#include <winsock.h>

namespace HBX {

namespace Str {
class Buffer;
}

/**
 * HTTP client for Windows Mobile
 *
 * Two transports share this interface:
 *   - WinSock (default, also the host-testable path): plain HTTP/1.1.
 *   - WinInet (device build, HBX_USE_WININET): HTTP and HTTPS.
 *
 * Request bodies are sent as UTF-8 and response bodies are decoded from UTF-8,
 * so text outside US-ASCII survives a round trip on both transports.
 *
 * THREADING -- one request at a time per instance.
 * An HttpClient owns a single socket (or a single set of WinInet handles) and
 * one header list, so it cannot service two overlapping requests. The internal
 * critical section keeps the header list and the socket handle from being
 * corrupted when two threads reach the same instance (on the device the EMDK
 * scanner thread and the UI thread both do), but it does not make concurrent
 * use meaningful: the second caller blocks for up to the configured timeout,
 * and headers installed by one thread apply to whichever request holds the
 * lock. Callers that genuinely need parallel requests must use one instance
 * per thread.
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    /**
     * Result of an HTTP request.
     * On success, body is heap-allocated with new TCHAR[] and ownership is
     * transferred to the caller, who must release it with delete[].
     */
    struct HttpResponse {
        int statusCode;
        TCHAR* body;

        HttpResponse() : statusCode(0), body(NULL) {}
    };

    // HTTP methods. Return true only when a COMPLETE response was received
    // (inspect response->statusCode for the HTTP status); false on network
    // failure, on a malformed reply, and on a body that was cut short before
    // the declared Content-Length / final chunk arrived.
    bool Get(const TCHAR* url, HttpResponse* response);
    bool Post(const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Put(const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Delete(const TCHAR* url, HttpResponse* response);

    // Configuration
    void SetTimeout(DWORD timeoutMs);
    void AddHeader(const TCHAR* key, const TCHAR* value);
    void ClearHeaders();

    /**
     * URL parsing (exposed for unit testing; pure helper with no side effects).
     * hostMax / pathMax are the capacities of the caller's buffers in TCHARs
     * including the NUL; they default to the sizes every caller in this code
     * base uses. Returns false -- rather than truncating -- when either part
     * does not fit, because a shortened host resolves to the wrong server.
     */
    bool ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path,
                  int hostMax = 256, int pathMax = 1024);

    // Status
    int GetLastHttpStatusCode() const;
    const TCHAR* GetLastError() const;

private:
    // Header storage structure
    struct HttpHeader {
        TCHAR* key;
        TCHAR* value;
        HttpHeader* next;
    };

    SOCKET m_socket;
    DWORD m_timeoutMs;
    int m_lastStatusCode;
    TCHAR* m_lastError;
    HttpHeader* m_headers;
    CRITICAL_SECTION m_lock;

    // Internal request handling
    bool SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response);
#ifdef HBX_USE_WININET
    // WinInet-based transport providing real HTTP and HTTPS (TLS) support.
    // Compiled only when HBX_USE_WININET is defined (the Windows Mobile device
    // build). The WinSock SendRequest above remains the host-testable default.
    bool SendRequestWinInet(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response);
#endif
    bool Connect(const TCHAR* host, int port);
    void Disconnect();

    // Header management. Both expect m_lock to be held by the caller.
    void AppendHeaderLines(Str::Buffer* out) const;
    bool HasHeader(const TCHAR* key) const;

    // Records a diagnostic for GetLastError(); NULL clears it.
    void SetError(const TCHAR* message);

    // Not copyable: the instance owns a socket, a header list and a lock.
    HttpClient(const HttpClient&);
    HttpClient& operator=(const HttpClient&);
};

} // namespace HBX

#endif // HTTPCLIENT_HPP
