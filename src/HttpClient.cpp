#include "../include/HttpClient.hpp"
#include <string.h>

namespace HBX {

HttpClient::HttpClient()
    : m_socket(INVALID_SOCKET)
    , m_timeoutMs(Common::DEFAULT_HTTP_TIMEOUT_MS)
    , m_userAgent(NULL)
    , m_lastStatusCode(0)
    , m_lastError(HTTP_ERROR_NONE)
    , m_lastErrorMessage(NULL)
    , m_headers(NULL)
{
    // Initialize WinSock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Set default user agent
    SetUserAgent(TEXT("HBXClient/1.0 (Windows Mobile 6.5)"));
}

HttpClient::~HttpClient()
{
    Disconnect();
    FreeHeaders();
    Common::StrFree(&m_lastErrorMessage);
    Common::StrFree(&m_userAgent);
    WSACleanup();
}

bool HttpClient::Get(const TCHAR* url, HttpResponse* response)
{
    return SendRequest(TEXT("GET"), url, NULL, response);
}

bool HttpClient::Post(const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    return SendRequest(TEXT("POST"), url, body, response);
}

bool HttpClient::Put(const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    return SendRequest(TEXT("PUT"), url, body, response);
}

bool HttpClient::Delete(const TCHAR* url, HttpResponse* response)
{
    return SendRequest(TEXT("DELETE"), url, NULL, response);
}

void HttpClient::SetTimeout(DWORD timeoutMs)
{
    m_timeoutMs = timeoutMs;
}

DWORD HttpClient::GetTimeout() const
{
    return m_timeoutMs;
}

void HttpClient::SetUserAgent(const TCHAR* userAgent)
{
    Common::StrFree(&m_userAgent);
    if (userAgent) {
        m_userAgent = Common::StrDuplicate(userAgent);
    }
}

void HttpClient::AddHeader(const TCHAR* key, const TCHAR* value)
{
    if (Common::IsNullOrEmpty(key) || !value) {
        return;
    }

    // Check if header already exists and update it
    HttpHeader* existing = FindHeader(key);
    if (existing) {
        Common::StrFree(&existing->value);
        existing->value = Common::StrDuplicate(value);
        return;
    }

    // Create new header node
    HttpHeader* newHeader = new HttpHeader();
    newHeader->key = Common::StrDuplicate(key);
    newHeader->value = Common::StrDuplicate(value);

    // Add to front of list
    newHeader->next = m_headers;
    m_headers = newHeader;
}

void HttpClient::RemoveHeader(const TCHAR* key)
{
    if (Common::IsNullOrEmpty(key)) {
        return;
    }

    HttpHeader* current = m_headers;
    HttpHeader* prev = NULL;

    while (current) {
        if (lstrcmpi(current->key, key) == 0) {
            // Found the header to remove
            if (prev) {
                prev->next = current->next;
            } else {
                m_headers = current->next;
            }
            delete current;
            return;
        }
        prev = current;
        current = current->next;
    }
}

void HttpClient::ClearHeaders()
{
    FreeHeaders();
}

HttpError HttpClient::GetLastError() const
{
    return m_lastError;
}

const TCHAR* HttpClient::GetLastErrorMessage() const
{
    return m_lastErrorMessage;
}

int HttpClient::GetLastHttpStatusCode() const
{
    return m_lastStatusCode;
}

bool HttpClient::SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    if (!response) {
        SetError(HTTP_ERROR_INVALID_URL, TEXT("Response parameter is NULL"));
        return false;
    }

    // Clear previous error
    ClearError();

    // Initialize response
    response->statusCode = 0;
    response->error = HTTP_ERROR_NONE;
    Common::StrFree(&response->body);
    Common::StrFree(&response->contentType);

    // Validate URL
    if (!Common::IsValidUrl(url)) {
        SetError(HTTP_ERROR_INVALID_URL, TEXT("Invalid URL format"));
        response->error = HTTP_ERROR_INVALID_URL;
        return false;
    }

    // Parse URL
    TCHAR host[Common::MAX_HOST_LENGTH];
    TCHAR path[Common::MAX_PATH_LENGTH];
    int port;

    if (!ParseUrl(url, host, &port, path)) {
        SetError(HTTP_ERROR_INVALID_URL, TEXT("Failed to parse URL"));
        response->error = HTTP_ERROR_INVALID_URL;
        return false;
    }

    // Connect to server
    if (!Connect(host, port)) {
        SetError(HTTP_ERROR_CONNECTION_FAILED, TEXT("Failed to connect to server"));
        response->error = HTTP_ERROR_CONNECTION_FAILED;
        return false;
    }

    // Build HTTP request
    char request[Common::MAX_HTTP_REQUEST_SIZE];
    char asciiMethod[16], asciiPath[Common::MAX_PATH_LENGTH];

    // Convert to ASCII
    Common::TCharToChar(method, asciiMethod, 16);
    Common::TCharToChar(path, asciiPath, Common::MAX_PATH_LENGTH);

    // Build request line
    sprintf(request, "%s %s HTTP/1.1\r\n", asciiMethod, asciiPath);

    // Add headers
    char headerStr[2048];
    BuildHeaderString(headerStr, 2048, host);
    strcat(request, headerStr);

    // Add body if present
    if (body && lstrlen(body) > 0) {
        char asciiBody[4096];
        int bodyLen = Common::TCharToChar(body, asciiBody, 4096);

        char contentLen[64];
        sprintf(contentLen, "Content-Length: %d\r\n", bodyLen);
        strcat(request, contentLen);
        strcat(request, "\r\n");
        strcat(request, asciiBody);
    } else {
        strcat(request, "\r\n");
    }

    // Send request
    int sent = send(m_socket, request, strlen(request), 0);
    if (sent <= 0) {
        Disconnect();
        SetError(HTTP_ERROR_SEND_FAILED, TEXT("Failed to send request"));
        response->error = HTTP_ERROR_SEND_FAILED;
        return false;
    }

    // Receive response
    char recvBuffer[Common::MAX_HTTP_RESPONSE_SIZE];
    int totalReceived = 0;
    int received;

    while ((received = recv(m_socket, recvBuffer + totalReceived,
            sizeof(recvBuffer) - totalReceived - 1, 0)) > 0) {
        totalReceived += received;
        if (totalReceived >= sizeof(recvBuffer) - 1) {
            break;
        }
        // Check if response is complete (look for end of headers)
        if (strstr(recvBuffer, "\r\n\r\n")) {
            // For simple responses, we might be done
            // In production, would check Content-Length
            break;
        }
    }

    if (received < 0) {
        Disconnect();
        SetError(HTTP_ERROR_RECEIVE_FAILED, TEXT("Failed to receive response"));
        response->error = HTTP_ERROR_RECEIVE_FAILED;
        return false;
    }

    recvBuffer[totalReceived] = '\0';
    Disconnect();

    // Parse response
    if (!ParseResponse(recvBuffer, totalReceived, response)) {
        SetError(HTTP_ERROR_INVALID_RESPONSE, TEXT("Failed to parse response"));
        response->error = HTTP_ERROR_INVALID_RESPONSE;
        return false;
    }

    m_lastStatusCode = response->statusCode;

    // Return true only if HTTP request succeeded (regardless of status code)
    return (response->error == HTTP_ERROR_NONE);
}

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
    char asciiHost[Common::MAX_HOST_LENGTH];
    Common::TCharToChar(host, asciiHost, Common::MAX_HOST_LENGTH);

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
    *port = Common::DEFAULT_HTTP_PORT;

    // Skip protocol (http:// or https://)
    const TCHAR* start = url;
    if (wcsncmp(url, TEXT("http://"), 7) == 0) {
        start = url + 7;
        *port = Common::DEFAULT_HTTP_PORT;
    } else if (wcsncmp(url, TEXT("https://"), 8) == 0) {
        start = url + 8;
        *port = Common::DEFAULT_HTTPS_PORT;
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

bool HttpClient::ParseResponse(const char* rawResponse, int responseLen, HttpResponse* response)
{
    if (!rawResponse || !response) {
        return false;
    }

    // Parse status code
    if (strncmp(rawResponse, "HTTP/1.", 7) == 0) {
        response->statusCode = atoi(rawResponse + 9);
    } else {
        return false;
    }

    // Find response body (after headers)
    const char* bodyStart = strstr(rawResponse, "\r\n\r\n");
    if (bodyStart) {
        bodyStart += 4;

        // Calculate body length
        int bodyLen = responseLen - (bodyStart - rawResponse);

        // Convert response to TCHAR
        response->body = new TCHAR[bodyLen + 1];
        Common::CharToTChar(bodyStart, response->body, bodyLen + 1);
        response->contentLength = bodyLen;
    }

    // Parse Content-Type header (optional)
    const char* contentTypeStart = strstr(rawResponse, "Content-Type:");
    if (contentTypeStart) {
        contentTypeStart += 13; // Skip "Content-Type:"
        while (*contentTypeStart == ' ') contentTypeStart++;

        const char* contentTypeEnd = strstr(contentTypeStart, "\r\n");
        if (contentTypeEnd) {
            int typeLen = contentTypeEnd - contentTypeStart;
            char tempType[128];
            if (typeLen < 128) {
                strncpy(tempType, contentTypeStart, typeLen);
                tempType[typeLen] = '\0';

                response->contentType = new TCHAR[typeLen + 1];
                Common::CharToTChar(tempType, response->contentType, typeLen + 1);
            }
        }
    }

    return true;
}

void HttpClient::BuildHeaderString(char* buffer, int maxLen, const TCHAR* host)
{
    if (!buffer || maxLen <= 0) {
        return;
    }

    buffer[0] = '\0';
    int pos = 0;

    // Add Host header
    char asciiHost[Common::MAX_HOST_LENGTH];
    Common::TCharToChar(host, asciiHost, Common::MAX_HOST_LENGTH);
    pos += sprintf(buffer + pos, "Host: %s\r\n", asciiHost);

    // Add User-Agent header
    if (m_userAgent) {
        char asciiUA[256];
        Common::TCharToChar(m_userAgent, asciiUA, 256);
        pos += sprintf(buffer + pos, "User-Agent: %s\r\n", asciiUA);
    }

    // Add custom headers
    HttpHeader* current = m_headers;
    while (current && pos < maxLen - 100) {
        char asciiKey[128], asciiValue[512];

        Common::TCharToChar(current->key, asciiKey, 128);
        Common::TCharToChar(current->value, asciiValue, 512);

        pos += sprintf(buffer + pos, "%s: %s\r\n", asciiKey, asciiValue);
        current = current->next;
    }
}

void HttpClient::FreeHeaders()
{
    while (m_headers) {
        HttpHeader* next = m_headers->next;
        delete m_headers;
        m_headers = next;
    }
}

HttpClient::HttpHeader* HttpClient::FindHeader(const TCHAR* key)
{
    if (Common::IsNullOrEmpty(key)) {
        return NULL;
    }

    HttpHeader* current = m_headers;
    while (current) {
        if (lstrcmpi(current->key, key) == 0) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

void HttpClient::SetError(HttpError error, const TCHAR* message)
{
    m_lastError = error;
    Common::StrFree(&m_lastErrorMessage);
    if (message) {
        m_lastErrorMessage = Common::StrDuplicate(message);
    }
}

void HttpClient::ClearError()
{
    m_lastError = HTTP_ERROR_NONE;
    Common::StrFree(&m_lastErrorMessage);
}

} // namespace HBX
