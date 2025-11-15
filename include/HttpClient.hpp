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

    // HTTP methods
    bool Get(const TCHAR* url, TCHAR* response, DWORD maxResponseLen);
    bool Post(const TCHAR* url, const TCHAR* body, TCHAR* response, DWORD maxResponseLen);
    bool Put(const TCHAR* url, const TCHAR* body, TCHAR* response, DWORD maxResponseLen);
    bool Delete(const TCHAR* url, TCHAR* response, DWORD maxResponseLen);

    // Configuration
    void SetTimeout(DWORD timeoutMs);
    void SetHeader(const TCHAR* key, const TCHAR* value);

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
    bool SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, TCHAR* response, DWORD maxResponseLen);
    bool Connect(const TCHAR* host, int port);
    void Disconnect();
    bool ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path);

    // Header management
    void ClearHeaders();
    void BuildHeaderString(char* buffer, int maxLen);
};

} // namespace HBX

#endif // HTTPCLIENT_HPP
