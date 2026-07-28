#include "../include/HttpClient.hpp"
#include "../include/StrUtil.hpp"
#include <string.h>

#ifdef HBX_USE_WININET
// Real HTTP/HTTPS transport via the Windows Internet (WinInet) API. This header
// is only pulled in for the device build; the host build never defines the
// macro and therefore keeps using the WinSock SendRequest path below.
#include <wininet.h>

// Windows CE ships a subset of the desktop wininet.h, and which constants made
// the cut varies between SDK revisions. The values are fixed by the API, so
// supplying any that are missing is safer than having the device build fail on
// a header we cannot inspect from here.
#ifndef INTERNET_FLAG_NO_COOKIES
#define INTERNET_FLAG_NO_COOKIES              0x00080000
#endif
#ifndef INTERNET_FLAG_NO_AUTO_REDIRECT
#define INTERNET_FLAG_NO_AUTO_REDIRECT        0x00200000
#endif
#ifndef INTERNET_FLAG_NO_UI
#define INTERNET_FLAG_NO_UI                   0x00000200
#endif
#ifndef INTERNET_FLAG_IGNORE_CERT_CN_INVALID
#define INTERNET_FLAG_IGNORE_CERT_CN_INVALID  0x00001000
#endif
#ifndef INTERNET_FLAG_IGNORE_CERT_DATE_INVALID
#define INTERNET_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
#endif
#ifndef INTERNET_OPTION_SECURITY_FLAGS
#define INTERNET_OPTION_SECURITY_FLAGS        31
#endif
#ifndef SECURITY_FLAG_IGNORE_UNKNOWN_CA
#define SECURITY_FLAG_IGNORE_UNKNOWN_CA       0x00000100
#endif
#ifndef SECURITY_FLAG_IGNORE_CERT_CN_INVALID
#define SECURITY_FLAG_IGNORE_CERT_CN_INVALID  0x00001000
#endif
#ifndef SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
#define SECURITY_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
#endif
#ifndef SECURITY_FLAG_IGNORE_WRONG_USAGE
#define SECURITY_FLAG_IGNORE_WRONG_USAGE      0x00000200
#endif
#endif // HBX_USE_WININET

#if !defined(UNDER_CE) && !defined(_WIN32) && !defined(WIN32)
// POSIX takes SO_RCVTIMEO / SO_SNDTIMEO as a struct timeval, not as the DWORD
// of milliseconds WinSock expects (see Connect).
#include <sys/time.h>
#endif

namespace HBX {

namespace {

// Hard ceiling on one HTTP response. The MC75 has very little usable heap, so
// a runaway (or hostile) reply must not be allowed to grow until the allocator
// gives up: it is reported as a failed request instead.
const int kMaxResponseBytes = 512 * 1024;

// Framing state of a chunked body.
const int kChunkIncomplete = 0;
const int kChunkComplete   = 1;
const int kChunkMalformed  = -1;

// WinInet error codes worth explaining to the operator. Spelled out here rather
// than taken from <wininet.h> so DescribeWinInetError compiles in the host build
// too, where that header is not included at all; the values are part of the
// published API and cannot move.
const DWORD kWinInetTimeout            = 12002;
const DWORD kWinInetNameNotResolved    = 12007;
const DWORD kWinInetCannotConnect      = 12029;
const DWORD kWinInetConnectionAborted  = 12030;
const DWORD kWinInetConnectionReset    = 12031;
const DWORD kWinInetCertDateInvalid    = 12037;
const DWORD kWinInetCertCnInvalid      = 12038;
const DWORD kWinInetClientCertNeeded   = 12044;
const DWORD kWinInetInvalidCa          = 12045;
const DWORD kWinInetCertRevFailed      = 12057;
const DWORD kWinInetSecureChannelError = 12157;
const DWORD kWinInetDisconnected       = 12163;
const DWORD kWinInetServerUnreachable  = 12164;
const DWORD kWinInetInvalidCert        = 12169;
const DWORD kWinInetCertRevoked        = 12170;

/**
 * Growable byte buffer for raw wire bytes. Always keeps a NUL one past the
 * last byte, so anything that scans the buffer as a C string stops at the end
 * of the received data instead of running into uninitialised memory.
 */
class ByteBuffer {
public:
    ByteBuffer() : m_data(NULL), m_len(0), m_cap(0), m_failed(false) {}
    ~ByteBuffer() { delete[] m_data; }

    bool Append(const char* data, int n)
    {
        if (n <= 0) {
            return !m_failed;
        }
        if (!Reserve(n)) {
            return false;
        }
        for (int i = 0; i < n; i++) {
            m_data[m_len + i] = data[i];
        }
        m_len += n;
        m_data[m_len] = '\0';
        return true;
    }

    // NULL while empty; every caller checks Length() first.
    char* Data() { return m_data; }
    int Length() const { return m_len; }
    bool Failed() const { return m_failed; }

private:
    bool Reserve(int extra)
    {
        if (m_failed) {
            return false;
        }
        int needed = m_len + extra + 1;
        if (needed <= m_cap) {
            return true;
        }

        int newCap = (m_cap > 0) ? m_cap : 4096;
        while (newCap < needed) {
            if (newCap > kMaxResponseBytes) {
                m_failed = true;
                return false;
            }
            newCap *= 2;
        }

        char* grown = new char[newCap];
        if (!grown) {
            m_failed = true;
            return false;
        }
        for (int i = 0; i < m_len; i++) {
            grown[i] = m_data[i];
        }
        grown[m_len] = '\0';

        delete[] m_data;
        m_data = grown;
        m_cap = newCap;
        return true;
    }

    char* m_data;
    int m_len;
    int m_cap;
    bool m_failed;

    ByteBuffer(const ByteBuffer&);
    ByteBuffer& operator=(const ByteBuffer&);
};

// Enters a critical section for the duration of a scope. Win32/WinCE critical
// sections are recursive, so a helper that re-locks is harmless.
class ScopedLock {
public:
    explicit ScopedLock(CRITICAL_SECTION* cs) : m_cs(cs) { EnterCriticalSection(m_cs); }
    ~ScopedLock() { LeaveCriticalSection(m_cs); }

private:
    CRITICAL_SECTION* m_cs;

    ScopedLock(const ScopedLock&);
    ScopedLock& operator=(const ScopedLock&);
};

// Index just past the "\r\n\r\n" that ends the header block, or -1 when the
// headers are not complete yet. Scans only the bytes actually received.
int FindHeaderEnd(const char* buf, int len)
{
    if (!buf) {
        return -1;
    }
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    return -1;
}

// Case-insensitive ASCII comparison of the span [p, p+len) with `lower`, which
// must already be lowercase. Header names are ASCII by definition.
bool SpanEqualsNoCase(const char* p, int len, const char* lower)
{
    int i = 0;
    for (; i < len && lower[i] != '\0'; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != lower[i]) {
            return false;
        }
    }
    return (i == len && lower[i] == '\0');
}

// Pulls the framing headers out of the response header block. contentLength is
// -1 when absent; a value larger than the response ceiling is clamped so the
// caller rejects it rather than overflowing while counting.
void ParseFramingHeaders(const char* buf, int headerEnd, long* contentLength, bool* chunked)
{
    *contentLength = -1;
    *chunked = false;

    int i = 0;
    // Skip the status line.
    while (i + 1 < headerEnd && !(buf[i] == '\r' && buf[i + 1] == '\n')) {
        i++;
    }
    i += 2;

    while (i < headerEnd) {
        int lineStart = i;
        while (i + 1 < headerEnd && !(buf[i] == '\r' && buf[i + 1] == '\n')) {
            i++;
        }
        int lineEnd = i;
        i += 2;

        if (lineEnd <= lineStart) {
            break; // empty line: end of the header block
        }

        int colon = lineStart;
        while (colon < lineEnd && buf[colon] != ':') {
            colon++;
        }
        if (colon >= lineEnd) {
            continue;
        }

        int valueStart = colon + 1;
        while (valueStart < lineEnd && (buf[valueStart] == ' ' || buf[valueStart] == '\t')) {
            valueStart++;
        }

        if (SpanEqualsNoCase(buf + lineStart, colon - lineStart, "content-length")) {
            long value = 0;
            bool anyDigit = false;
            for (int k = valueStart; k < lineEnd && buf[k] >= '0' && buf[k] <= '9'; k++) {
                if (value > kMaxResponseBytes) {
                    value = (long)kMaxResponseBytes + 1;
                    anyDigit = true;
                    break;
                }
                value = value * 10 + (buf[k] - '0');
                anyDigit = true;
            }
            if (anyDigit) {
                *contentLength = value;
            }
        } else if (SpanEqualsNoCase(buf + lineStart, colon - lineStart, "transfer-encoding")) {
            // "chunked" is always the final coding when it is present at all.
            for (int k = valueStart; k + 7 <= lineEnd; k++) {
                if (SpanEqualsNoCase(buf + k, 7, "chunked")) {
                    *chunked = true;
                    break;
                }
            }
        }
    }
}

/**
 * Walks a chunked body starting at `start`. When `out` is non-NULL the
 * de-framed bytes are written there (decoding only ever shrinks the data, so
 * writing back into the same buffer at `start` is safe). Returns
 * kChunkComplete once the terminating zero-length chunk is seen,
 * kChunkIncomplete while more bytes are needed, kChunkMalformed on a chunk
 * header that is not hexadecimal or is larger than the response ceiling.
 */
int ScanChunkedBody(const char* buf, int start, int len, char* out, int* outLen)
{
    int pos = start;
    int written = 0;

    for (;;) {
        int lineEnd = -1;
        for (int i = pos; i + 1 < len; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n') {
                lineEnd = i;
                break;
            }
        }
        if (lineEnd < 0) {
            return kChunkIncomplete;
        }

        long size = 0;
        bool anyDigit = false;
        for (int i = pos; i < lineEnd; i++) {
            char c = buf[i];
            int digit;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            } else {
                break; // chunk extension (";...") or trailing whitespace
            }
            size = size * 16 + digit;
            anyDigit = true;
            if (size > kMaxResponseBytes) {
                return kChunkMalformed;
            }
        }
        if (!anyDigit) {
            return kChunkMalformed;
        }

        int dataStart = lineEnd + 2;
        if (size == 0) {
            // Trailers may follow; the payload is complete either way.
            if (outLen) {
                *outLen = written;
            }
            return kChunkComplete;
        }
        if (dataStart + (int)size + 2 > len) {
            return kChunkIncomplete; // this chunk (or its CRLF) has not arrived
        }

        if (out) {
            memmove(out + written, buf + dataStart, (size_t)size);
        }
        written += (int)size;
        pos = dataStart + (int)size + 2;
    }
}

// Extracts the numeric status from "HTTP/1.1 200 OK". Returns 0 when the reply
// does not start with a status line we understand.
int ParseStatusLine(const char* buf, int len)
{
    if (len < 12 || strncmp(buf, "HTTP/", 5) != 0) {
        return 0;
    }

    int i = 0;
    while (i < len && buf[i] != ' ' && buf[i] != '\r') {
        i++;
    }
    while (i < len && buf[i] == ' ') {
        i++;
    }

    int code = 0;
    int digits = 0;
    while (i < len && digits < 3 && buf[i] >= '0' && buf[i] <= '9') {
        code = code * 10 + (buf[i] - '0');
        i++;
        digits++;
    }
    return (digits == 3) ? code : 0;
}

// ASCII case-insensitive TCHAR compare, used to detect headers the caller has
// already supplied. lstrcmpi is locale-aware on the device and unavailable in
// this form on the host shim, so the comparison is spelled out.
bool TcharEqualsNoCase(const TCHAR* a, const TCHAR* b)
{
    if (!a || !b) {
        return false;
    }
    for (int i = 0; ; i++) {
        TCHAR ca = a[i];
        TCHAR cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (TCHAR)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (TCHAR)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        if (ca == 0) {
            return true;
        }
    }
}

} // namespace

HttpClient::HttpClient()
    : m_socket(INVALID_SOCKET)
    , m_timeoutMs(30000)
    , m_lastStatusCode(0)
    , m_lastError(NULL)
    , m_headers(NULL)
    , m_ignoreCertErrors(false)
{
    m_lastMethod[0] = 0;

    InitializeCriticalSection(&m_lock);

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

    DeleteCriticalSection(&m_lock);
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

// Both transports take the verb as a string, so PATCH costs nothing beyond
// this line: WinSock writes it into the request line, WinInet hands it to
// HttpOpenRequest.
bool HttpClient::Patch(const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    return HBX_SEND_REQUEST(TEXT("PATCH"), url, body, response);
}

bool HttpClient::Delete(const TCHAR* url, HttpResponse* response)
{
    return HBX_SEND_REQUEST(TEXT("DELETE"), url, NULL, response);
}

void HttpClient::SetTimeout(DWORD timeoutMs)
{
    ScopedLock guard(&m_lock);
    m_timeoutMs = timeoutMs;
}

void HttpClient::SetIgnoreCertificateErrors(bool ignore)
{
    ScopedLock guard(&m_lock);
    m_ignoreCertErrors = ignore;
}

bool HttpClient::IgnoreCertificateErrors() const
{
    return m_ignoreCertErrors;
}

void HttpClient::AddHeader(const TCHAR* key, const TCHAR* value)
{
    if (!key || !value) {
        return;
    }

    ScopedLock guard(&m_lock);

    // Create new header node
    HttpHeader* newHeader = new HttpHeader();
    if (!newHeader) {
        return;
    }

    newHeader->key = Str::Dup(key);
    newHeader->value = Str::Dup(value);
    if (!newHeader->key || !newHeader->value) {
        delete[] newHeader->key;
        delete[] newHeader->value;
        delete newHeader;
        return;
    }

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

const TCHAR* HttpClient::GetLastMethod() const
{
    return m_lastMethod;
}

void HttpClient::RecordMethod(const TCHAR* method)
{
    Str::Copy(m_lastMethod, (int)kMethodChars, method ? method : TEXT(""));
}

void HttpClient::SetError(const TCHAR* message)
{
    if (m_lastError) {
        delete[] m_lastError;
        m_lastError = NULL;
    }
    if (message) {
        m_lastError = Str::Dup(message);
    }
}

bool HttpClient::HasHeader(const TCHAR* key) const
{
    for (HttpHeader* h = m_headers; h != NULL; h = h->next) {
        if (TcharEqualsNoCase(h->key, key)) {
            return true;
        }
    }
    return false;
}

void HttpClient::AppendHeaderLines(Str::Buffer* out) const
{
    for (HttpHeader* h = m_headers; h != NULL; h = h->next) {
        out->Append(h->key);
        out->Append(TEXT(": "));
        out->Append(h->value);
        out->Append(TEXT("\r\n"));
    }
}

bool HttpClient::SendRequest(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    if (!response) {
        return false;
    }
    response->statusCode = 0;
    response->body = NULL;

    ScopedLock guard(&m_lock);

    // Recorded first so a diagnostic can name the verb even when the request
    // never reaches the wire.
    RecordMethod(method);

    // Parse URL
    TCHAR host[256];
    TCHAR path[1024];
    int port;

    if (!ParseUrl(url, host, &port, path, 256, 1024)) {
        SetError(TEXT("Malformed or oversized URL"));
        return false;
    }

    // Connect to server
    if (!Connect(host, port)) {
        return false;
    }

    // Build the request as text first; it is encoded to UTF-8 in one pass
    // below, so a body outside US-ASCII reaches the server intact.
    Str::Buffer request;
    request.Append(method);
    request.AppendChar((TCHAR)' ');
    request.Append(path);
    request.Append(TEXT(" HTTP/1.1\r\nHost: "));
    request.Append(host);
    if (port != 80) {
        request.AppendChar((TCHAR)':');
        request.AppendInt(port);
    }
    request.Append(TEXT("\r\n"));

    AppendHeaderLines(&request);

    // Ask for JSON unless the caller has its own opinion. Both backends answer
    // JSON by default, but NetBox's DRF layer also has an HTML "browsable API"
    // renderer that wins whenever a client sends a browser-style Accept; that
    // page is orders of magnitude larger than the JSON and would blow the
    // response ceiling on a device with no heap to spare.
    //
    // Deliberately no "version=" parameter. NetBox uses AcceptHeaderVersioning
    // with ALLOWED_VERSIONS containing only the version actually installed, so
    // any pin answers 406 Not Acceptable -- and would break the client the day
    // the server is upgraded.
    if (!HasHeader(TEXT("Accept"))) {
        request.Append(TEXT("Accept: application/json\r\n"));
    }

    // Read-to-close is the fallback framing for a reply that carries neither
    // Content-Length nor chunked encoding, so ask the server to close.
    if (!HasHeader(TEXT("Connection"))) {
        request.Append(TEXT("Connection: close\r\n"));
    }

    int bodyBytes = 0;
    if (body && body[0] != '\0') {
        bodyBytes = Str::Utf8Size(body) - 1; // Content-Length counts wire bytes
        request.Append(TEXT("Content-Length: "));
        request.AppendInt(bodyBytes);
        request.Append(TEXT("\r\n"));
        if (!HasHeader(TEXT("Content-Type"))) {
            request.Append(TEXT("Content-Type: application/json; charset=utf-8\r\n"));
        }
    }
    request.Append(TEXT("\r\n"));
    if (bodyBytes > 0) {
        request.Append(body);
    }

    if (request.Failed()) {
        Disconnect();
        SetError(TEXT("Out of memory building request"));
        return false;
    }

    char* wire = Str::ToUtf8Alloc(request.Get());
    if (!wire) {
        Disconnect();
        SetError(TEXT("Out of memory encoding request"));
        return false;
    }

    // send() is allowed to accept only part of the buffer.
    int wireLen = (int)strlen(wire);
    int totalSent = 0;
    bool sendFailed = false;
    while (totalSent < wireLen) {
        int sent = send(m_socket, wire + totalSent, wireLen - totalSent, 0);
        if (sent <= 0) {
            sendFailed = true;
            break;
        }
        totalSent += sent;
    }
    delete[] wire;

    if (sendFailed) {
        Disconnect();
        SetError(TEXT("Send failed"));
        return false;
    }

    // Receive the response. Headers and body are not guaranteed to arrive in
    // the same segment, so reading stops only once the framing says the body
    // is complete (Content-Length satisfied, final chunk seen) or the peer
    // closes the connection.
    ByteBuffer raw;
    char chunk[2048];
    int headerEnd = -1;
    long contentLength = -1;
    bool chunked = false;
    bool overflow = false;

    for (;;) {
        if (headerEnd < 0) {
            headerEnd = FindHeaderEnd(raw.Data(), raw.Length());
            if (headerEnd >= 0) {
                ParseFramingHeaders(raw.Data(), headerEnd, &contentLength, &chunked);
            }
        }
        if (headerEnd >= 0) {
            if (chunked) {
                if (ScanChunkedBody(raw.Data(), headerEnd, raw.Length(), NULL, NULL) != kChunkIncomplete) {
                    break;
                }
            } else if (contentLength >= 0) {
                if ((long)(raw.Length() - headerEnd) >= contentLength) {
                    break;
                }
            }
            // Neither framing header present: read until the peer closes.
        }

        if (raw.Length() >= kMaxResponseBytes) {
            overflow = true;
            break;
        }

        int received = recv(m_socket, chunk, (int)sizeof(chunk), 0);
        if (received <= 0) {
            break; // peer closed, or the receive timeout expired
        }
        if (!raw.Append(chunk, received)) {
            overflow = true;
            break;
        }
    }

    Disconnect();

    if (overflow || raw.Failed()) {
        SetError(TEXT("Response too large"));
        return false;
    }
    if (raw.Length() == 0) {
        SetError(TEXT("No response received"));
        return false;
    }

    if (headerEnd < 0) {
        headerEnd = FindHeaderEnd(raw.Data(), raw.Length());
        if (headerEnd >= 0) {
            ParseFramingHeaders(raw.Data(), headerEnd, &contentLength, &chunked);
        }
    }
    if (headerEnd < 0) {
        SetError(TEXT("Incomplete response headers"));
        return false;
    }

    // Parse status code
    m_lastStatusCode = ParseStatusLine(raw.Data(), raw.Length());
    response->statusCode = m_lastStatusCode;

    // Resolve the body. A body that stopped short of its declared length is a
    // failure, not a short JSON document handed to the caller.
    char* bodyStart = raw.Data() + headerEnd;
    int available = raw.Length() - headerEnd;
    int bodyLen;

    if (chunked) {
        int decoded = 0;
        if (ScanChunkedBody(raw.Data(), headerEnd, raw.Length(), bodyStart, &decoded) != kChunkComplete) {
            SetError(TEXT("Malformed or truncated chunked response"));
            return false;
        }
        bodyLen = decoded;
    } else if (contentLength >= 0) {
        if (contentLength > (long)available) {
            SetError(TEXT("Response body truncated"));
            return false;
        }
        bodyLen = (int)contentLength;
    } else {
        bodyLen = available;
    }

    // Safe: the buffer always keeps one spare byte past Length().
    bodyStart[bodyLen] = '\0';

    // The wire is UTF-8; hand the caller a decoded, heap-owned TCHAR body.
    response->body = Str::FromUtf8Alloc(bodyStart);
    if (!response->body) {
        SetError(TEXT("Out of memory decoding response"));
        return false;
    }

    SetError(NULL);
    return true;
}

#ifdef HBX_USE_WININET
// WinInet transport: performs a real HTTP or HTTPS request. TLS is negotiated
// when, and only when, the URL names the https scheme. All handles are released
// on every return path.
bool HttpClient::SendRequestWinInet(const TCHAR* method, const TCHAR* url, const TCHAR* body, HttpResponse* response)
{
    if (!response) {
        return false;
    }
    response->statusCode = 0;
    response->body = NULL;

    ScopedLock guard(&m_lock);

    // Recorded first so a diagnostic can name the verb even when the request
    // never reaches the wire.
    RecordMethod(method);

    // Parse URL into host / port / path.
    TCHAR host[256];
    TCHAR path[1024];
    int port = 0;
    if (!ParseUrl(url, host, &port, path, 256, 1024)) {
        SetError(TEXT("Malformed or oversized URL"));
        return false;
    }

    const bool isHttps = IsSecureUrl(url);

    // Open a WinInet session.
    HINTERNET hInternet = InternetOpen(TEXT("HBXClient/1.0"),
                                       INTERNET_OPEN_TYPE_DIRECT,
                                       NULL, NULL, 0);
    if (!hInternet) {
        SetError(TEXT("InternetOpen failed"));
        return false;
    }

    // SetTimeout() must reach the wire on the device too: WinInet ignores the
    // socket options the WinSock path uses and defaults to minutes-long waits,
    // which strands the UI thread on a dead GPRS link.
    DWORD timeout = m_timeoutMs;
    InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    // Connect to the target host/port over the HTTP service.
    HINTERNET hConnect = InternetConnect(hInternet, host, (INTERNET_PORT)port,
                                         NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        SetError(TEXT("InternetConnect failed"));
        return false;
    }

    // Build the request. Every flag below is load-bearing:
    //
    //   RELOAD             never serve this from the WinInet cache.
    //
    //   NO_COOKIES         NetBox lists DRF's SessionAuthentication BEFORE
    //                      TokenAuthentication. WinInet stores and replays
    //                      cookies for the life of the session by default, so
    //                      one stray Set-Cookie: sessionid makes session auth
    //                      match first on every later request -- which turns on
    //                      CSRF enforcement, which nothing here can satisfy, so
    //                      every write comes back "403 CSRF Failed". Token auth
    //                      needs no cookie at all. This flag looks removable and
    //                      is not.
    //
    //   NO_AUTO_REDIRECT   NetBox requires a trailing slash on API URLs and
    //                      answers 301 without one. Letting WinInet follow that
    //                      redirect re-issues the write as a GET: the call
    //                      reports 200 and nothing was changed. Not following it
    //                      also keeps this transport consistent with the WinSock
    //                      one, which has never followed redirects -- otherwise
    //                      the same misconfigured URL would behave differently on
    //                      the device and in the host tests.
    //
    //   NO_UI              this runs on the sync thread; a modal certificate
    //                      prompt there hangs the handheld with nobody to answer
    //                      it.
    DWORD requestFlags = INTERNET_FLAG_RELOAD |
                         INTERNET_FLAG_NO_COOKIES |
                         INTERNET_FLAG_NO_AUTO_REDIRECT |
                         INTERNET_FLAG_NO_UI;
    if (isHttps) {
        requestFlags |= INTERNET_FLAG_SECURE;
        if (m_ignoreCertErrors) {
            // Opt-in only; see SetIgnoreCertificateErrors() for what this costs.
            requestFlags |= INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
                            INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
        }
    }

    HINTERNET hRequest = HttpOpenRequest(hConnect, method, path,
                                         NULL, NULL, NULL, requestFlags, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        SetError(TEXT("HttpOpenRequest failed"));
        return false;
    }

    InternetSetOption(hRequest, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOption(hRequest, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOption(hRequest, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    if (isHttps && m_ignoreCertErrors) {
        // An untrusted CA is the one certificate failure with no HttpOpenRequest
        // flag behind it -- it can only be waived through the security flags,
        // and only on the request handle (the session handle answers with
        // ERROR_INTERNET_INCORRECT_HANDLE_TYPE). Doing it before the first send
        // avoids the documented fail-then-retry dance, which on some CE builds
        // needs the request handle torn down and rebuilt to take effect.
        DWORD securityFlags = 0;
        DWORD securityLen = sizeof(securityFlags);
        if (!InternetQueryOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS,
                                 &securityFlags, &securityLen)) {
            securityFlags = 0;
        }
        securityFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_WRONG_USAGE;
        InternetSetOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS,
                          &securityFlags, sizeof(securityFlags));
    }

    // Add the accumulated custom headers as a single "Key: Value\r\n" block.
    Str::Buffer headerBlock;
    AppendHeaderLines(&headerBlock);
    // HttpOpenRequest was given no accept types, so WinInet sends no Accept
    // header of its own. Supply one for the same reason as the WinSock path:
    // keep NetBox's HTML browsable-API renderer out of a 512 KB response budget.
    // Never with a "version=" parameter -- NetBox answers 406 to any version but
    // the one it is running.
    if (!HasHeader(TEXT("Accept"))) {
        headerBlock.Append(TEXT("Accept: application/json\r\n"));
    }
    if (body && body[0] != '\0' && !HasHeader(TEXT("Content-Type"))) {
        headerBlock.Append(TEXT("Content-Type: application/json; charset=utf-8\r\n"));
    }
    if (headerBlock.Failed()) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        SetError(TEXT("Out of memory building headers"));
        return false;
    }
    if (headerBlock.Length() > 0) {
        HttpAddRequestHeaders(hRequest, headerBlock.Get(),
                              (DWORD)headerBlock.Length(), HTTP_ADDREQ_FLAG_ADD);
    }

    // The body goes on the wire as UTF-8; the byte count comes from the
    // conversion, not from the TCHAR length.
    char* bodyBytes = NULL;
    DWORD bodyByteLen = 0;
    if (body && body[0] != '\0') {
        bodyBytes = Str::ToUtf8Alloc(body);
        if (!bodyBytes) {
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            SetError(TEXT("Out of memory encoding request"));
            return false;
        }
        bodyByteLen = (DWORD)strlen(bodyBytes);
    }

    BOOL sent = HttpSendRequest(hRequest, NULL, 0, (LPVOID)bodyBytes, bodyByteLen);
    if (bodyBytes) {
        delete[] bodyBytes;
        bodyBytes = NULL;
    }

    if (!sent) {
        // Read the code before anything else: InternetCloseHandle succeeds and
        // resets the thread's last-error, so cleaning up first would throw away
        // the only evidence of why the send failed. Reporting a bare
        // "HttpSendRequest failed" is what made every TLS problem on this device
        // -- untrusted CA, wrong host name, expired certificate, a server that
        // will not speak TLS 1.0 -- look identical to a dead network.
        DWORD lastError = ::GetLastError();

        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);

        const TCHAR* detail = DescribeWinInetError(lastError);
        if (detail) {
            SetError(detail);
        } else {
            Str::Buffer msg;
            msg.Append(TEXT("HttpSendRequest failed (WinInet error "));
            msg.AppendInt((long)lastError);
            msg.AppendChar((TCHAR)')');
            SetError(msg.Failed() ? TEXT("HttpSendRequest failed") : msg.Get());
        }
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

    // Read the response body. A read error part way through means the body is
    // incomplete: reporting it as a finished response would hand truncated
    // JSON to the caller as if the server had sent it.
    ByteBuffer raw;
    char readBuf[2048];
    DWORD bytesRead = 0;
    bool readFailed = false;

    for (;;) {
        if (!InternetReadFile(hRequest, readBuf, (DWORD)sizeof(readBuf), &bytesRead)) {
            readFailed = true;
            break;
        }
        if (bytesRead == 0) {
            break;
        }
        if (raw.Length() + (int)bytesRead > kMaxResponseBytes) {
            readFailed = true;
            break;
        }
        if (!raw.Append(readBuf, (int)bytesRead)) {
            readFailed = true;
            break;
        }
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (readFailed) {
        SetError(TEXT("Response read failed or too large"));
        return false;
    }

    // WinInet already de-frames chunked transfers, so the bytes are the body.
    response->body = Str::FromUtf8Alloc(raw.Length() > 0 ? raw.Data() : "");
    if (!response->body) {
        SetError(TEXT("Out of memory decoding response"));
        return false;
    }

    SetError(NULL);
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
        SetError(TEXT("Socket creation failed"));
        return false;
    }

    // Set timeout. WinSock (device) takes a DWORD of milliseconds; POSIX takes
    // a struct timeval and rejects the DWORD form outright, which is why the
    // host build used to run with no timeout at all.
#if defined(UNDER_CE) || defined(_WIN32) || defined(WIN32)
    DWORD timeout = m_timeoutMs;
    int rcvOk = setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    int sndOk = setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = (long)(m_timeoutMs / 1000);
    tv.tv_usec = (long)((m_timeoutMs % 1000) * 1000);
    int rcvOk = setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    int sndOk = setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif
    if (rcvOk == SOCKET_ERROR || sndOk == SOCKET_ERROR) {
        // Not fatal, but a request can now block for the stack default; record
        // it so a hung sync has a diagnostic.
        SetError(TEXT("Socket timeout not applied"));
    }

    // Host names are ASCII; the UTF-8 encoder keeps the buffer terminated at
    // the copied length instead of leaving an uninitialised tail.
    char asciiHost[256];
    if (!Str::ToUtf8(asciiHost, (int)sizeof(asciiHost), host)) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        SetError(TEXT("Host name too long"));
        return false;
    }

    // Resolve hostname
    struct hostent* hostInfo = gethostbyname(asciiHost);
    if (!hostInfo) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        SetError(TEXT("Host name could not be resolved"));
        return false;
    }

    // Setup address structure
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons((unsigned short)port);
    serverAddr.sin_addr = *((struct in_addr*)hostInfo->h_addr);

    // Connect
    if (connect(m_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        SetError(TEXT("Connection failed"));
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

bool HttpClient::ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path,
                          int hostMax, int pathMax)
{
    if (!url || !host || !port || !path || hostMax < 2 || pathMax < 2) {
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

    // Extract host. Truncation is rejected rather than silently accepted: a
    // shortened host name resolves to a different server, or to nothing.
    // Every failure path below clears the outputs before returning, so a caller
    // that neglects to check the result gets an empty host that fails fast
    // rather than a plausible-looking wrong one.
    bool ok = true;

    if (portStart && (!pathStart || portStart < pathStart)) {
        // Port specified
        ok = Str::CopyN(host, hostMax, start, (int)(portStart - start));

        if (ok) {
            int parsedPort = _wtoi(portStart + 1);
            if (parsedPort <= 0 || parsedPort > 65535) {
                ok = false;
            } else {
                *port = parsedPort;
                // Find path after port
                pathStart = wcschr(portStart, '/');
            }
        }
    } else if (pathStart) {
        // No port, path specified
        ok = Str::CopyN(host, hostMax, start, (int)(pathStart - start));
    } else {
        // No port, no path
        ok = Str::Copy(host, hostMax, start);
    }

    // Extract path (query string included)
    if (ok && pathStart) {
        ok = Str::Copy(path, pathMax, pathStart);
    }

    if (!ok || lstrlen(host) == 0) {
        host[0] = '\0';
        path[0] = '/';
        path[1] = '\0';
        return false;
    }

    return true;
}

bool HttpClient::IsSecureUrl(const TCHAR* url)
{
    // Scheme only. The previous test also accepted "port == 443", which meant a
    // plain listener reached as http://host:443 -- a perfectly ordinary shape
    // for the HTTP-only reverse proxy this device is expected to talk to -- was
    // pushed through a TLS handshake it could never complete. ParseUrl already
    // supplies 443 as the default port for an https URL, so the port carries no
    // information the scheme does not.
    return url != NULL && wcsncmp(url, TEXT("https://"), 8) == 0;
}

const TCHAR* HttpClient::DescribeWinInetError(DWORD code)
{
    switch (code) {
        // --- TLS -----------------------------------------------------------
        // The MC75's Schannel tops out at SSL 3.0 / TLS 1.0 with RC4, DES or
        // 3DES, and its root store dates from around 2009. Every one of these
        // is a likely first encounter for someone pointing the handheld at a
        // current HTTPS endpoint, so each says what actually went wrong and
        // what to do about it.
        case kWinInetSecureChannelError:
            return TEXT("TLS handshake failed (12157). This device offers only ")
                   TEXT("SSL 3.0 / TLS 1.0 and a modern server will refuse it. ")
                   TEXT("Use plain HTTP on a trusted LAN, or a proxy that ")
                   TEXT("accepts legacy TLS.");
        case kWinInetInvalidCa:
            return TEXT("TLS certificate authority not trusted (12045). This ")
                   TEXT("device's root store predates most current CAs. Install ")
                   TEXT("the CA on the device, or use plain HTTP on a trusted LAN.");
        case kWinInetCertCnInvalid:
            return TEXT("TLS certificate name does not match the server (12038). ")
                   TEXT("Use the exact host name the certificate was issued for.");
        case kWinInetCertDateInvalid:
            return TEXT("TLS certificate is expired or not yet valid (12037). ")
                   TEXT("Check the certificate dates and the device clock.");
        case kWinInetInvalidCert:
            return TEXT("TLS certificate is invalid (12169).");
        case kWinInetCertRevoked:
            return TEXT("TLS certificate has been revoked (12170).");
        case kWinInetCertRevFailed:
            return TEXT("TLS revocation check could not be completed (12057).");
        case kWinInetClientCertNeeded:
            return TEXT("Server requested a client certificate (12044); none is ")
                   TEXT("installed on this device.");

        // --- network -------------------------------------------------------
        case kWinInetTimeout:
            return TEXT("Request timed out (12002).");
        case kWinInetNameNotResolved:
            return TEXT("Server name could not be resolved (12007). This device ")
                   TEXT("has no mDNS, so a .local name will never resolve; use an ")
                   TEXT("IP address or a name your DNS serves.");
        case kWinInetCannotConnect:
            return TEXT("Cannot connect to the server (12029). Check the address, ")
                   TEXT("the port and that the server is listening.");
        case kWinInetConnectionAborted:
            return TEXT("Connection aborted by the server (12030).");
        case kWinInetConnectionReset:
            return TEXT("Connection reset by the server (12031).");
        case kWinInetDisconnected:
            return TEXT("No network connection (12163). Work will be queued.");
        case kWinInetServerUnreachable:
            return TEXT("Server unreachable (12164).");
        default:
            // Unknown code: the caller reports the number so it can still be
            // looked up, rather than inventing advice for it.
            return NULL;
    }
}

void HttpClient::ClearHeaders()
{
    ScopedLock guard(&m_lock);

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

} // namespace HBX
