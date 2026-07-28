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
 * REDIRECTS -- never followed, on either transport. A 3xx reaches the caller as
 * response->statusCode with the request left untouched. This matters for NetBox,
 * which answers 301 for an API URL missing its trailing slash: following that
 * redirect turns a PATCH into a GET, so the write reports success and changes
 * nothing. The WinSock path has never followed redirects; the WinInet path is
 * told not to, so the device and the host tests agree on a misconfigured URL.
 *
 * COOKIES -- never stored or replayed. Both backends authenticate with a header,
 * and a persisted session cookie would only serve to trip the server's CSRF
 * enforcement (see the WinInet request flags for the full story).
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

    /**
     * Partial update -- the only safe write verb for NetBox.
     *
     * A PUT there is a full object representation: every field the handheld
     * does not know about (tenant, platform, config template, custom fields,
     * comments...) would be sent back as absent and blanked. PATCH carries
     * exactly the keys in `body` and leaves the rest of the record alone.
     */
    bool Patch(const TCHAR* url, const TCHAR* body, HttpResponse* response);

    bool Delete(const TCHAR* url, HttpResponse* response);

    // Configuration
    void SetTimeout(DWORD timeoutMs);
    void AddHeader(const TCHAR* key, const TCHAR* value);
    void ClearHeaders();

    /**
     * Accept a TLS certificate this device cannot validate. WinInet transport
     * only; the WinSock transport has no TLS at all. DEFAULTS TO false, and an
     * operator has to opt in deliberately.
     *
     * Enabling it waives the unknown-CA, host-name and validity-date checks,
     * which is to say the client will accept ANY certificate from ANY party.
     * All authentication of the server is gone; only encryption against a
     * passive listener remains. An on-path attacker on the wireless segment can
     * terminate the connection transparently, and because the API token travels
     * in the Authorization header they capture a working credential on the very
     * first request. That is arguably worse than plain HTTP, because the URL
     * still says "https" and everyone stops thinking about it.
     *
     * The one defensible use is a trusted, isolated LAN whose server presents a
     * private certificate that the MC75's 2009-vintage root store cannot chain.
     * Anywhere else, plain HTTP on a segmented VLAN keeps the trade-off honest.
     */
    void SetIgnoreCertificateErrors(bool ignore);

    /** True when certificate validation has been waived; for a UI warning. */
    bool IgnoreCertificateErrors() const;

    /**
     * URL parsing (exposed for unit testing; pure helper with no side effects).
     * hostMax / pathMax are the capacities of the caller's buffers in TCHARs
     * including the NUL; they default to the sizes every caller in this code
     * base uses. Returns false -- rather than truncating -- when either part
     * does not fit, because a shortened host resolves to the wrong server.
     */
    bool ParseUrl(const TCHAR* url, TCHAR* host, int* port, TCHAR* path,
                  int hostMax = 256, int pathMax = 1024);

    /**
     * Whether `url` names the https scheme, and therefore whether the WinInet
     * transport must negotiate TLS.
     *
     * The scheme is the whole answer. This used to also treat "port == 443" as
     * https, so `http://host:443` -- a plain listener that happens to sit on
     * 443, which is exactly what an HTTP-only reverse proxy for this device may
     * look like -- was sent through a TLS handshake that could never complete.
     * ParseUrl already defaults an https URL to port 443, so keying off the
     * scheme alone loses nothing. Like ParseUrl, only the lowercase form is
     * recognised.
     */
    static bool IsSecureUrl(const TCHAR* url);

    /**
     * Human-readable description of a WinInet error code (the value
     * ::GetLastError() returns after a failed WinInet call), or NULL when the
     * code is not one we have specific advice for.
     *
     * Compiled into every build, not just the device build, so the mapping is
     * covered by the host tests. The TLS entries carry the most weight: this
     * device offers only SSL 3.0 / TLS 1.0 with RC4 or 3DES and has a root
     * store from ~2009, so pointing it at a modern HTTPS endpoint fails in ways
     * that are indistinguishable from "the network is down" unless the code is
     * reported. The returned pointer is a static literal; do not free it.
     */
    static const TCHAR* DescribeWinInetError(DWORD code);

    // Status
    int GetLastHttpStatusCode() const;
    const TCHAR* GetLastError() const;

    /**
     * Verb of the most recent request attempt, "" before the first one.
     * Recorded before any parsing or network work, so it is set even for a
     * request that never left the device -- which is what makes a failure log
     * read "PATCH: Malformed or oversized URL" instead of just the message.
     */
    const TCHAR* GetLastMethod() const;

private:
    // Header storage structure
    struct HttpHeader {
        TCHAR* key;
        TCHAR* value;
        HttpHeader* next;
    };

    // Longest verb this client sends is "DELETE"; sized with room to spare so
    // the copy is bounded without ever needing the heap on a per-scan path.
    enum { kMethodChars = 12 };

    SOCKET m_socket;
    DWORD m_timeoutMs;
    int m_lastStatusCode;
    TCHAR* m_lastError;
    HttpHeader* m_headers;
    CRITICAL_SECTION m_lock;
    bool m_ignoreCertErrors;
    TCHAR m_lastMethod[kMethodChars];

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

    // Stores the verb for GetLastMethod(); expects m_lock to be held.
    void RecordMethod(const TCHAR* method);

    // Records a diagnostic for GetLastError(); NULL clears it.
    void SetError(const TCHAR* message);

    // Not copyable: the instance owns a socket, a header list and a lock.
    HttpClient(const HttpClient&);
    HttpClient& operator=(const HttpClient&);
};

} // namespace HBX

#endif // HTTPCLIENT_HPP
