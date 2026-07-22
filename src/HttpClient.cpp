#include "../include/HttpClient.hpp"
#include <string.h>

#ifdef HBX_USE_WININET
// Real HTTP/HTTPS transport via the Windows Internet (WinInet) API. This header
// is only pulled in for the device build; the host build never defines the
// macro and therefore keeps using the WinSock SendRequest path below.
#include <wininet.h>
#endif

namespace HBX {

HttpClient::HttpClient()
    : m_socket(INVALID_SOCKET)
    , m_timeoutMs(30000)
    , m_lastStatusCode(0)
    , m_lastError(NULL)
    , m_headers(NULL)
{
    // Initialize WinSock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

HttpClient::~HttpClient()
{
    Disconnect();
    ClearHeaders();
    if (m_lastError) {
        delete[] m_lastError;
    }
    WSACleanup();
}

// Each verb dispatches to the WinInet transport when HBX_USE_WININET is defined
// (device build, real HTTP + HTTPS), otherwise to the WinSock SendRequest path
// (host-testable default, HTTP only).
#ifdef HBX_USE_WININET
#define HBX_SEND_REQUEST(method, url, body, response) SendRequestWinInet((method), (url), (body), (response))
#else
#define HBX_SEND_REQUEST(method, url, body, response) SendRequest((method), (url), (body), (response))
#endif

bool HttpClient::Get(const TCHAR* url, HttpResponse* response)
{
    return HBX_SEND_REQUEST(TEXT("GET"), url, NULL, response);
}

bool HttpClient::Post(const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    return HBX_SEND_REQUEST(TEXT("POST"), url, body, response);
}

bool HttpClient::Put(const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    return HBX_SEND_REQUEST(TEXT("PUT"), url, body, response);
}

bool HttpClient::Delete(const TCHAR* url, HttpResponse* response)
{
    return HBX_SEND_REQUEST(TEXT("DELETE"), url, NULL, response);
}

void HttpClient::SetTimeout(DWORD timeoutMs)
{
    m_timeoutMs = timeoutMs;
}

void HttpClient::AddHeader(const TCHAR* key, const TCHAR* value)
{
    if (!key || !value) {
        return;
    }

    // Create new header node
    HttpHeader* newHeader = new HttpHeader();

    int keyLen = lstrlen(key) + 1;
    newHeader->key = new TCHAR[keyLen];
    lstrcpy(newHeader->key, key);

    int valueLen = lstrlen(value) + 1;
    newHeader->value = new TCHAR[valueLen];
    lstrcpy(newHeader->value, value);

    // Add to front of list
    newHeader->next = m_headers;
    m_headers = newHeader;
}

int HttpClient::GetLastHttpStatusCode() const
{
    return m_lastStatusCode;
}

const TCHAR* HttpClient::GetLastError() const
{
    return m_lastError;
}

bool HttpClient::SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    if (!response) {
        return false;
    }
    response->statusCode = 0;
    response->body = NULL;

    // Parse URL
    TCHAR host[256];
    TCHAR path[1024];
    int port;

    if (!ParseUrl(url, host, &port, path)) {
        return false;
    }

    // Connect to server
    if (!Connect(host, port)) {
        return false;
    }

    // Build HTTP request
    char request[4096];
    char asciiMethod[16], asciiPath[1024], asciiHost[256];

    // Convert to ASCII
    for (int i = 0; i < 15 && method[i] != '\0'; i++) {
        asciiMethod[i] = (char)method[i];
    }
    asciiMethod[15] = '\0';

    for (int i = 0; i < 1023 && path[i] != '\0'; i++) {
        asciiPath[i] = (char)path[i];
    }
    asciiPath[1023] = '\0';

    for (int i = 0; i < 255 && host[i] != '\0'; i++) {
        asciiHost[i] = (char)host[i];
    }
    asciiHost[255] = '\0';

    // Build request line
    sprintf(request, "%s %s HTTP/1.1\r\nHost: %s\r\n", asciiMethod, asciiPath, asciiHost);

    // Add custom headers
    char headerStr[1024];
    BuildHeaderString(headerStr, 1024);
    strcat(request, headerStr);

    // Add body if present
    if (body && lstrlen(body) > 0) {
        char asciiBody[2048];
        int bodyLen = 0;
        for (int i = 0; i < 2047 && body[i] != '\0'; i++) {
            asciiBody[i] = (char)body[i];
            bodyLen++;
        }
        asciiBody[bodyLen] = '\0';

        char contentLen[64];
        sprintf(contentLen, "Content-Length: %d\r\n", bodyLen);
        strcat(request, contentLen);
        strcat(request, "Content-Type: application/json\r\n");
        strcat(request, "\r\n");
        strcat(request, asciiBody);
    } else {
        strcat(request, "\r\n");
    }

    // Send request
    int sent = send(m_socket, request, strlen(request), 0);
    if (sent <= 0) {
        Disconnect();
        return false;
    }

    // Receive response
    char recvBuffer[8192];
    int totalReceived = 0;
    int received;

    while ((received = recv(m_socket, recvBuffer + totalReceived, sizeof(recvBuffer) - totalReceived - 1, 0)) > 0) {
        totalReceived += received;
        if (totalReceived >= sizeof(recvBuffer) - 1) {
            break;
        }
        // Simple check if response is complete (look for end of headers)
        if (strstr(recvBuffer, "\r\n\r\n")) {
            break;
        }
    }

    recvBuffer[totalReceived] = '\0';
    Disconnect();

    // No bytes received means the request effectively failed.
    if (totalReceived == 0) {
        return false;
    }

    // Parse status code
    m_lastStatusCode = 0;
    if (strncmp(recvBuffer, "HTTP/1.", 7) == 0) {
        m_lastStatusCode = atoi(recvBuffer + 9);
    }
    response->statusCode = m_lastStatusCode;

    // Find response body (after end-of-headers marker)
    const char* bodyStart = strstr(recvBuffer, "\r\n\r\n");
    bodyStart = bodyStart ? (bodyStart + 4) : "";

    // Copy body into a heap buffer owned by the caller (converts ASCII -> TCHAR).
    int bodyLen = (int)strlen(bodyStart);
    response->body = new TCHAR[bodyLen + 1];
    for (int i = 0; i < bodyLen; i++) {
        response->body[i] = (TCHAR)bodyStart[i];
    }
    response->body[bodyLen] = '\0';

    // A response was received; the caller inspects response->statusCode.
    return true;
}

#ifdef HBX_USE_WININET
// WinInet transport: performs a real HTTP or HTTPS request. HTTPS (TLS) is
// selected transparently when the URL uses the https scheme (default port 443)
// or explicitly names port 443. All handles are released on every return path.
bool HttpClient::SendRequestWinInet(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    if (!response) {
        return false;
    }
    response->statusCode = 0;
    response->body = NULL;

    // Parse URL into host / port / path.
    TCHAR host[256];
    TCHAR path[1024];
    int port = 0;
    if (!ParseUrl(url, host, &port, path)) {
        return false;
    }

    // Use TLS for https URLs (default 443 or an explicit https:// scheme).
    bool isHttps = (port == 443) || (wcsncmp(url, TEXT("https://"), 8) == 0);

    // Open a WinInet session.
    HINTERNET hInternet = InternetOpen(TEXT("HBXClient/1.0"),
                                       INTERNET_OPEN_TYPE_DIRECT,
                                       NULL, NULL, 0);
    if (!hInternet) {
        return false;
    }

    // Connect to the target host/port over the HTTP service.
    HINTERNET hConnect = InternetConnect(hInternet, host, (INTERNET_PORT)port,
                                         NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }

    // Build the request; enable TLS and bypass the cache.
    DWORD requestFlags = INTERNET_FLAG_RELOAD;
    if (isHttps) {
        requestFlags |= INTERNET_FLAG_SECURE;
    }

    HINTERNET hRequest = HttpOpenRequest(hConnect, method, path,
                                         NULL, NULL, NULL, requestFlags, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }

    // Add the accumulated custom headers as a single "Key: Value\r\n" block.
    int headerTotal = 0;
    for (HttpHeader* h = m_headers; h != NULL; h = h->next) {
        // key + ": " + value + "\r\n"
        headerTotal += lstrlen(h->key) + 2 + lstrlen(h->value) + 2;
    }
    if (headerTotal > 0) {
        TCHAR* headerBuf = new TCHAR[headerTotal + 1];
        headerBuf[0] = '\0';
        for (HttpHeader* h = m_headers; h != NULL; h = h->next) {
            lstrcat(headerBuf, h->key);
            lstrcat(headerBuf, TEXT(": "));
            lstrcat(headerBuf, h->value);
            lstrcat(headerBuf, TEXT("\r\n"));
        }
        HttpAddRequestHeaders(hRequest, headerBuf,
                              (DWORD)lstrlen(headerBuf), HTTP_ADDREQ_FLAG_ADD);
        delete[] headerBuf;
    }

    // Convert the (Unicode) body to a narrow byte buffer for the wire.
    char* bodyBytes = NULL;
    DWORD bodyByteLen = 0;
    if (body && lstrlen(body) > 0) {
        int bl = lstrlen(body);
        bodyBytes = new char[bl];
        for (int i = 0; i < bl; i++) {
            bodyBytes[i] = (char)body[i];
        }
        bodyByteLen = (DWORD)bl;
    }

    BOOL sent = HttpSendRequest(hRequest, NULL, 0, (LPVOID)bodyBytes, bodyByteLen);
    if (bodyBytes) {
        delete[] bodyBytes;
        bodyBytes = NULL;
    }

    if (!sent) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return false;
    }

    // Query the numeric HTTP status code.
    DWORD statusCode = 0;
    DWORD statusLen = sizeof(statusCode);
    DWORD statusIndex = 0;
    if (HttpQueryInfo(hRequest,
                      HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                      &statusCode, &statusLen, &statusIndex)) {
        m_lastStatusCode = (int)statusCode;
    } else {
        m_lastStatusCode = 0;
    }
    response->statusCode = m_lastStatusCode;

    // Read the response body into a growable byte buffer.
    char* data = NULL;
    int dataLen = 0;
    int dataCap = 0;
    char readBuf[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hRequest, readBuf, (DWORD)sizeof(readBuf), &bytesRead)) {
        if (bytesRead == 0) {
            break;
        }
        if (dataLen + (int)bytesRead > dataCap) {
            int newCap = (dataCap == 0) ? 8192 : dataCap * 2;
            while (newCap < dataLen + (int)bytesRead) {
                newCap *= 2;
            }
            char* newData = new char[newCap];
            for (int i = 0; i < dataLen; i++) {
                newData[i] = data[i];
            }
            if (data) {
                delete[] data;
            }
            data = newData;
            dataCap = newCap;
        }
        for (DWORD i = 0; i < bytesRead; i++) {
            data[dataLen + (int)i] = readBuf[i];
        }
        dataLen += (int)bytesRead;
    }

    // Hand the caller a heap TCHAR body (ASCII widen), owned by the caller.
    response->body = new TCHAR[dataLen + 1];
    for (int i = 0; i < dataLen; i++) {
        response->body[i] = (TCHAR)data[i];
    }
    response->body[dataLen] = '\0';
    if (data) {
        delete[] data;
    }

    // Close all handles (no leaks) and report that a response was received.
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return true;
}
#endif // HBX_USE_WININET

bool HttpClient::Connect(const TCHAR* host, int port)
{
    // Disconnect if already connected
    Disconnect();

    // Create socket
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        return false;
    }

    // Set timeout
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&m_timeoutMs, sizeof(m_timeoutMs));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&m_timeoutMs, sizeof(m_timeoutMs));

    // Convert host to ASCII for gethostbyname
    char asciiHost[256];
    for (int i = 0; i < 255 && host[i] != '\0'; i++) {
        asciiHost[i] = (char)host[i];
    }
    asciiHost[255] = '\0';

    // Resolve hostname
    struct hostent* hostInfo = gethostbyname(asciiHost);
    if (!hostInfo) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    // Setup address structure
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr = *((struct in_addr*)hostInfo->h_addr);

    // Connect
    if (connect(m_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    return true;
}

void HttpClient::Disconnect()
{
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

bool HttpClient::ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path)
{
    if (!url || !host || !port || !path) {
        return false;
    }

    // Initialize outputs
    host[0] = '\0';
    path[0] = '/';
    path[1] = '\0';
    *port = 80; // Default HTTP port

    // Skip protocol (http:// or https://)
    const TCHAR* start = url;
    if (wcsncmp(url, TEXT("http://"), 7) == 0) {
        start = url + 7;
        *port = 80;
    } else if (wcsncmp(url, TEXT("https://"), 8) == 0) {
        start = url + 8;
        *port = 443;
    }

    // Find the first slash (path separator) or colon (port separator)
    const TCHAR* pathStart = wcschr(start, '/');
    const TCHAR* portStart = wcschr(start, ':');

    // Extract host
    int hostLen;
    if (portStart && (!pathStart || portStart < pathStart)) {
        // Port specified
        hostLen = (int)(portStart - start);
        wcsncpy(host, start, hostLen);
        host[hostLen] = '\0';

        // Extract port
        *port = _wtoi(portStart + 1);

        // Find path after port
        pathStart = wcschr(portStart, '/');
    } else if (pathStart) {
        // No port, path specified
        hostLen = (int)(pathStart - start);
        wcsncpy(host, start, hostLen);
        host[hostLen] = '\0';
    } else {
        // No port, no path
        lstrcpy(host, start);
    }

    // Extract path
    if (pathStart) {
        lstrcpy(path, pathStart);
    }

    return (lstrlen(host) > 0);
}

void HttpClient::ClearHeaders()
{
    while (m_headers) {
        HttpHeader* next = m_headers->next;
        if (m_headers->key) {
            delete[] m_headers->key;
        }
        if (m_headers->value) {
            delete[] m_headers->value;
        }
        delete m_headers;
        m_headers = next;
    }
}

void HttpClient::BuildHeaderString(char* buffer, int maxLen)
{
    buffer[0] = '\0';
    int pos = 0;

    HttpHeader* current = m_headers;
    while (current && pos < maxLen - 100) {
        // Convert key and value to ASCII
        char asciiKey[128], asciiValue[512];

        for (int i = 0; i < 127 && current->key[i] != '\0'; i++) {
            asciiKey[i] = (char)current->key[i];
        }
        asciiKey[127] = '\0';

        for (int i = 0; i < 511 && current->value[i] != '\0'; i++) {
            asciiValue[i] = (char)current->value[i];
        }
        asciiValue[511] = '\0';

        // Add header to buffer
        pos += sprintf(buffer + pos, "%s: %s\r\n", asciiKey, asciiValue);

        current = current->next;
    }
}

} // namespace HBX
