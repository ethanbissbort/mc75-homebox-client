#ifndef HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#include <windows.h>
#include <winsock.h>
#include "Common.hpp"
#include "Errors.hpp"

namespace HBX {

/**
 * HTTP response structure
 * Contains the complete HTTP response data
 */
struct HttpResponse {
    int statusCode;           // HTTP status code (e.g., 200, 404)
    TCHAR* body;              // Response body (dynamically allocated, caller must delete[])
    TCHAR* contentType;       // Content-Type header value
    DWORD contentLength;      // Content length
    HttpError error;          // Error code if request failed

    HttpResponse()
        : statusCode(0)
        , body(NULL)
        , contentType(NULL)
        , contentLength(0)
        , error(HTTP_ERROR_NONE)
    {
    }

    ~HttpResponse()
    {
        if (body) {
            delete[] body;
            body = NULL;
        }
        if (contentType) {
            delete[] contentType;
            contentType = NULL;
        }
    }

    /**
     * Check if response indicates success
     * @return true if HTTP status 200-299
     */
    bool IsSuccess() const
    {
        return IsHttpSuccess(statusCode);
    }

    /**
     * Check if response body is valid
     * @return true if body is not NULL and not empty
     */
    bool HasBody() const
    {
        return !Common::IsNullOrEmpty(body);
    }

    /**
     * Get status description
     * @return Human-readable status string
     */
    const TCHAR* GetStatusDescription() const
    {
        return HttpStatusToString(statusCode);
    }
};

/**
 * HTTP client for Windows Mobile
 * Provides low-level HTTP communication using WinSock
 *
 * Modern API using HttpResponse structure for consistent error handling
 * and memory management.
 *
 * Usage:
 *   HttpClient client;
 *   client.AddHeader(TEXT("Authorization"), TEXT("Bearer token"));
 *
 *   HttpResponse response;
 *   if (client.Get(TEXT("http://api.example.com/items/123"), &response)) {
 *       if (response.IsSuccess()) {
 *           // Process response.body
 *       }
 *   }
 *   // HttpResponse destructor automatically frees body memory
 *
 * Thread Safety: Not thread-safe. Use separate instances per thread.
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // HTTP methods - Modern API using HttpResponse
    bool Get(const TCHAR* url, HttpResponse* response);
    bool Post(const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Put(const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Delete(const TCHAR* url, HttpResponse* response);

    // Configuration
    void SetTimeout(DWORD timeoutMs);
    DWORD GetTimeout() const;
    void SetUserAgent(const TCHAR* userAgent);

    // Header management
    void AddHeader(const TCHAR* key, const TCHAR* value);
    void RemoveHeader(const TCHAR* key);
    void ClearHeaders();

    // Status and errors
    HttpError GetLastError() const;
    const TCHAR* GetLastErrorMessage() const;
    int GetLastHttpStatusCode() const;

private:
    // Header storage structure
    struct HttpHeader {
        TCHAR* key;
        TCHAR* value;
        HttpHeader* next;

        HttpHeader()
            : key(NULL)
            , value(NULL)
            , next(NULL)
        {
        }

        ~HttpHeader()
        {
            Common::StrFree(&key);
            Common::StrFree(&value);
        }
    };

    SOCKET m_socket;
    DWORD m_timeoutMs;
    TCHAR* m_userAgent;
    int m_lastStatusCode;
    HttpError m_lastError;
    TCHAR* m_lastErrorMessage;
    HttpHeader* m_headers;

    // Internal request handling
    bool SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response);
    bool Connect(const TCHAR* host, int port);
    void Disconnect();
    bool ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path);
    bool ParseResponse(const char* rawResponse, int responseLen, HttpResponse* response);

    // Header management
    void BuildHeaderString(char* buffer, int maxLen, const TCHAR* host);
    void FreeHeaders();
    HttpHeader* FindHeader(const TCHAR* key);

    // Error handling
    void SetError(HttpError error, const TCHAR* message);
    void ClearError();
};

} // namespace HBX

#endif // HTTPCLIENT_HPP
