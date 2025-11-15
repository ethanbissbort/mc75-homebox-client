#include "../include/HttpClient.hpp"
#include <string.h>

namespace HBX {

HttpClient::HttpClient()
    : m_socket(INVALID_SOCKET)
    , m_timeoutMs(30000)
    , m_lastStatusCode(0)
    , m_lastError(NULL)
{
    // Initialize WinSock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

HttpClient::~HttpClient()
{
    Disconnect();
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
    // TODO: Store headers for requests
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
    // TODO: Implement HTTP request
    // Stub implementation
    m_lastStatusCode = 200;
    if (response && maxResponseLen > 0) {
        lstrcpy(response, TEXT("{}"));
    }
    return true;
}

bool HttpClient::Connect(const TCHAR* host, int port)
{
    // TODO: Implement socket connection
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
    // TODO: Implement URL parsing
    return true;
}

} // namespace HBX
