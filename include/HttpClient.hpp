#ifndef HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#include <windows.h>
#include <winsock.h>

namespace HBX {

/**
 * HTTP client for Windows Mobile
 * Provides low-level HTTP communication using WinSock
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

    // HTTP methods. Return true when a response was received (inspect
    // response->statusCode for the HTTP status); false on network failure.
    bool Get(const TCHAR* url, HttpResponse* response);
    bool Post(const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Put(const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Delete(const TCHAR* url, HttpResponse* response);

    // Configuration
    void SetTimeout(DWORD timeoutMs);
    void AddHeader(const TCHAR* key, const TCHAR* value);
    void ClearHeaders();

    // URL parsing (exposed for unit testing; pure helper with no side effects)
    bool ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path);

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

    // Header management
    void BuildHeaderString(char* buffer, int maxLen);
};

} // namespace HBX

#endif // HTTPCLIENT_HPP
