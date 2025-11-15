#include "../include/HttpClient.hpp"
#include <string.h>

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

bool HttpClient::Get(const TCHAR* url, TCHAR* response, DWORD maxResponseLen)
{
    return SendRequest(TEXT("GET"), url, NULL, response, maxResponseLen);
}

bool HttpClient::Post(const TCHAR* url, const TCHAR* body, TCHAR* response, DWORD maxResponseLen)
{
    return SendRequest(TEXT("POST"), url, body, response, maxResponseLen);
}

bool HttpClient::Put(const TCHAR* url, const TCHAR* body, TCHAR* response, DWORD maxResponseLen)
{
    return SendRequest(TEXT("PUT"), url, body, response, maxResponseLen);
}

bool HttpClient::Delete(const TCHAR* url, TCHAR* response, DWORD maxResponseLen)
{
    return SendRequest(TEXT("DELETE"), url, NULL, response, maxResponseLen);
}

void HttpClient::SetTimeout(DWORD timeoutMs)
{
    m_timeoutMs = timeoutMs;
}

void HttpClient::SetHeader(const TCHAR* key, const TCHAR* value)
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

bool HttpClient::SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, TCHAR* response, DWORD maxResponseLen)
{
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

    // Parse status code
    if (strncmp(recvBuffer, "HTTP/1.", 7) == 0) {
        m_lastStatusCode = atoi(recvBuffer + 9);
    }

    // Find response body (after headers)
    char* bodyStart = strstr(recvBuffer, "\r\n\r\n");
    if (bodyStart) {
        bodyStart += 4;

        // Convert response to TCHAR
        if (response && maxResponseLen > 0) {
            DWORD len = 0;
            while (*bodyStart && len < maxResponseLen - 1) {
                response[len++] = (TCHAR)*bodyStart++;
            }
            response[len] = '\0';
        }
    }

    return (m_lastStatusCode >= 200 && m_lastStatusCode < 300);
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
