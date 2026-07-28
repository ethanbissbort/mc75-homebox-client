// test_http.cpp -- Unit tests for HBX::HttpClient URL parsing and response struct.
//
// These tests exercise only the pure, network-free surface of HttpClient:
//   - ParseUrl(): protocol/host/port/path extraction (no sockets touched).
//   - IsSecureUrl(): the https/plain decision the WinInet transport keys off.
//   - DescribeWinInetError(): WinInet error code -> operator-facing diagnostic.
//   - HttpResponse: default-constructed field values.
//   - GetLastHttpStatusCode()/AddHeader()/ClearHeaders(): trivial config paths.
//
// Verb dispatch is checked through GetLastMethod() with a URL that cannot parse,
// so the request is abandoned before any socket work. Beyond that no
// Get/Post/Put/Patch/Delete call is made -- no server is available in the host
// build, and those methods would open real sockets.

#include "test_framework.hpp"
#include "HttpClient.hpp"
#include "StrUtil.hpp"

using namespace HBX;

TEST_CASE("ParseUrl: http with multi-segment path -> host, port 80, path") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    bool ok = hc.ParseUrl(TEXT("http://example.com/api/v1/items"), host, &port, path);
    CHECK(ok);
    CHECK_EQ_STR(host, TEXT("example.com"));
    CHECK_EQ_INT(port, 80);
    CHECK_EQ_STR(path, TEXT("/api/v1/items"));
}

TEST_CASE("ParseUrl: https defaults to port 443") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    bool ok = hc.ParseUrl(TEXT("https://secure.example.com/x"), host, &port, path);
    CHECK(ok);
    CHECK_EQ_STR(host, TEXT("secure.example.com"));
    CHECK_EQ_INT(port, 443);
    CHECK_EQ_STR(path, TEXT("/x"));
}

TEST_CASE("ParseUrl: explicit port over http") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    bool ok = hc.ParseUrl(TEXT("http://host.local:8080/p/q"), host, &port, path);
    CHECK(ok);
    CHECK_EQ_STR(host, TEXT("host.local"));
    CHECK_EQ_INT(port, 8080);
    CHECK_EQ_STR(path, TEXT("/p/q"));
}

TEST_CASE("ParseUrl: explicit port over https") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    bool ok = hc.ParseUrl(TEXT("https://api.example.com:8443/v1"), host, &port, path);
    CHECK(ok);
    CHECK_EQ_STR(host, TEXT("api.example.com"));
    CHECK_EQ_INT(port, 8443);
    CHECK_EQ_STR(path, TEXT("/v1"));
}

TEST_CASE("ParseUrl: no path defaults to '/' and still succeeds") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    bool ok = hc.ParseUrl(TEXT("http://bare.example.com"), host, &port, path);
    CHECK(ok);
    CHECK_EQ_STR(host, TEXT("bare.example.com"));
    CHECK_EQ_INT(port, 80);
    CHECK_EQ_STR(path, TEXT("/"));
}

TEST_CASE("ParseUrl: NULL url returns false") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = 0;

    bool ok = hc.ParseUrl(NULL, host, &port, path);
    CHECK_FALSE(ok);
}

TEST_CASE("HttpResponse: default-constructed fields are zero/NULL") {
    HttpClient::HttpResponse r;
    CHECK_EQ_INT(r.statusCode, 0);
    CHECK(r.body == NULL);
}

TEST_CASE("HttpClient: status code starts at 0; header add/clear does not crash") {
    HttpClient hc2;
    CHECK_EQ_INT(hc2.GetLastHttpStatusCode(), 0);
    hc2.AddHeader(TEXT("A"), TEXT("b"));
    hc2.ClearHeaders();
    // Reaching here without crashing is the assertion.
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Regression tests for the hardened ParseUrl contract.
//
// ParseUrl used to copy the host with an unbounded wcsncpy/lstrcpy into the
// caller's TCHAR host[256]; a base URL with a long host label overran the
// caller's stack frame (reproduced under ASAN as a 400-TCHAR write). It now
// takes the buffer capacities and REFUSES rather than truncating, because a
// shortened host silently resolves to the wrong server -- a far worse outcome
// than a clean failure.
// ---------------------------------------------------------------------------

// Builds "http://<len 'a's>/p" into caller storage.
static void MakeLongHostUrl(TCHAR* out, int cap, int hostLen)
{
    out[0] = 0;
    HBX::Str::Append(out, cap, TEXT("http://"));
    int base = lstrlen(out);
    for (int i = 0; i < hostLen && base + i < cap - 3; i++) {
        out[base + i] = (TCHAR)'a';
    }
    out[base + hostLen] = 0;
    HBX::Str::Append(out, cap, TEXT("/p"));
}

TEST_CASE("ParseUrl: an over-long host is rejected, not truncated") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    // Poison the destination so a partial write would be visible.
    for (int i = 0; i < 256; i++) {
        host[i] = (TCHAR)'#';
    }

    TCHAR url[1024];
    MakeLongHostUrl(url, 1024, 400);

    CHECK_FALSE(hc.ParseUrl(url, host, &port, path));

    // A host that does not fit must not be silently shortened to something
    // that would resolve to a different server.
    CHECK(host[255] == (TCHAR)'#' || host[0] == 0);
}

TEST_CASE("ParseUrl: a host that exactly fills the buffer still parses") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    TCHAR url[1024];
    MakeLongHostUrl(url, 1024, 255); // 255 chars + NUL == the 256 capacity

    CHECK(hc.ParseUrl(url, host, &port, path));
    CHECK_EQ_INT(lstrlen(host), 255);
    CHECK_EQ_INT(port, 80);
    CHECK_EQ_STR(path, TEXT("/p"));
}

TEST_CASE("ParseUrl: explicit capacities are honoured") {
    HttpClient hc;
    TCHAR host[32];
    TCHAR path[32];
    int port = -1;

    // Fits comfortably inside the declared capacities.
    CHECK(hc.ParseUrl(TEXT("http://example.com/a"), host, &port, path, 32, 32));
    CHECK_EQ_STR(host, TEXT("example.com"));
    CHECK_EQ_STR(path, TEXT("/a"));

    // Host is 11 chars; a capacity of 8 cannot hold it.
    CHECK_FALSE(hc.ParseUrl(TEXT("http://example.com/a"), host, &port, path, 8, 32));

    // Path is longer than its capacity.
    CHECK_FALSE(hc.ParseUrl(TEXT("http://example.com/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
                            host, &port, path, 32, 8));
}

TEST_CASE("ParseUrl: query strings and ports survive parsing") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    CHECK(hc.ParseUrl(TEXT("https://api.example.com:8443/v1/items?barcode=123&x=1"),
                      host, &port, path));
    CHECK_EQ_STR(host, TEXT("api.example.com"));
    CHECK_EQ_INT(port, 8443);
    // The query must reach the server intact -- item lookup depends on it.
    CHECK_EQ_STR(path, TEXT("/v1/items?barcode=123&x=1"));
}

TEST_CASE("ParseUrl: malformed input is rejected rather than half-parsed") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    CHECK_FALSE(hc.ParseUrl(TEXT(""), host, &port, path));
    CHECK_FALSE(hc.ParseUrl(TEXT("http://"), host, &port, path));
    CHECK_FALSE(hc.ParseUrl(TEXT("://example.com"), host, &port, path));
}

// ---------------------------------------------------------------------------
// Verb dispatch.
//
// A malformed URL is rejected by ParseUrl before any socket is created, but the
// verb is recorded first -- which makes it the seam for checking that each
// public method reaches the transport with the right method string. PATCH is
// the one that matters: NetBox writes must be partial updates, because a PUT
// carries a full object representation and would blank every field the
// handheld does not know about (tenant, platform, custom fields, comments).
// ---------------------------------------------------------------------------

// A URL with no scheme and no host; ParseUrl refuses it, so nothing is sent.
static const TCHAR* const kUnsendableUrl = TEXT("://not-a-url");

TEST_CASE("HttpClient: no request attempted yet -> last method is empty") {
    HttpClient hc;
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT(""));
}

TEST_CASE("Patch: dispatches the PATCH verb to the transport") {
    HttpClient hc;
    HttpClient::HttpResponse r;

    CHECK_FALSE(hc.Patch(kUnsendableUrl, TEXT("{\"status\":\"inventory\"}"), &r));
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT("PATCH"));
    // The verb is recorded, then the URL is rejected -- nothing went on the wire.
    CHECK_EQ_STR(hc.GetLastError(), TEXT("Malformed or oversized URL"));
    CHECK_EQ_INT(r.statusCode, 0);
    CHECK(r.body == NULL);
}

TEST_CASE("Patch: a NULL response is refused without touching the transport") {
    HttpClient hc;
    CHECK_FALSE(hc.Patch(TEXT("http://example.com/api/dcim/devices/1/"),
                         TEXT("{}"), NULL));
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT(""));
}

TEST_CASE("Verbs: every method reaches the transport under its own name") {
    HttpClient hc;
    HttpClient::HttpResponse r;

    hc.Get(kUnsendableUrl, &r);
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT("GET"));

    hc.Post(kUnsendableUrl, TEXT("{}"), &r);
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT("POST"));

    hc.Put(kUnsendableUrl, TEXT("{}"), &r);
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT("PUT"));

    hc.Patch(kUnsendableUrl, TEXT("{}"), &r);
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT("PATCH"));

    hc.Delete(kUnsendableUrl, &r);
    CHECK_EQ_STR(hc.GetLastMethod(), TEXT("DELETE"));
}

// ---------------------------------------------------------------------------
// Scheme detection.
//
// The WinInet transport used to decide on TLS with
// `(port == 443) || scheme == https`, so a plain listener addressed as
// http://host:443 was pushed through a handshake it could never complete. Only
// the scheme decides now; ParseUrl still supplies 443 as the https default.
// ---------------------------------------------------------------------------

TEST_CASE("IsSecureUrl: the https scheme selects TLS") {
    CHECK(HttpClient::IsSecureUrl(TEXT("https://netbox.lan/api/dcim/devices/")));
    CHECK(HttpClient::IsSecureUrl(TEXT("https://netbox.lan:8443/api/")));
    // Port 443 is the https default, so the scheme alone still gets this right.
    CHECK(HttpClient::IsSecureUrl(TEXT("https://netbox.lan:443/api/")));
}

TEST_CASE("IsSecureUrl: port 443 alone does NOT make a URL https") {
    // The regression: a plain HTTP reverse proxy listening on 443 is a normal
    // deployment for this device, and treating it as TLS broke every request.
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("http://netbox.lan:443/api/")));
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("http://192.168.20.5:443/api/")));
}

TEST_CASE("IsSecureUrl: plain and malformed URLs are not secure") {
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("http://netbox.lan/api/")));
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("http://netbox.lan:8080/api/")));
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("netbox.lan/api/")));
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("https:/netbox.lan/")));
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("")));
    CHECK_FALSE(HttpClient::IsSecureUrl(NULL));
}

TEST_CASE("IsSecureUrl agrees with the port ParseUrl derives from the scheme") {
    HttpClient hc;
    TCHAR host[256];
    TCHAR path[1024];
    int port = -1;

    CHECK(hc.ParseUrl(TEXT("https://netbox.lan/api/"), host, &port, path));
    CHECK_EQ_INT(port, 443);
    CHECK(HttpClient::IsSecureUrl(TEXT("https://netbox.lan/api/")));

    CHECK(hc.ParseUrl(TEXT("http://netbox.lan:443/api/"), host, &port, path));
    CHECK_EQ_INT(port, 443);
    CHECK_FALSE(HttpClient::IsSecureUrl(TEXT("http://netbox.lan:443/api/")));
}

// ---------------------------------------------------------------------------
// TLS / transport error mapping.
//
// The WinInet path used to report the literal "HttpSendRequest failed" for
// every failure, discarding ::GetLastError(). On a device whose Schannel stops
// at TLS 1.0 and whose root store is from ~2009, that made a certificate the
// device cannot chain, a host name mismatch and a server that will not speak
// TLS 1.0 all look like a dead network.
// ---------------------------------------------------------------------------

// Non-NULL, non-empty and different from every other mapped code.
static void CheckDistinctDiagnostic(DWORD code, const DWORD* others, int otherCount)
{
    const TCHAR* text = HttpClient::DescribeWinInetError(code);
    CHECK(text != NULL);
    if (!text) {
        return;
    }
    CHECK(lstrlen(text) > 0);
    for (int i = 0; i < otherCount; i++) {
        if (others[i] == code) {
            continue;
        }
        const TCHAR* other = HttpClient::DescribeWinInetError(others[i]);
        CHECK(other != NULL && lstrcmp(text, other) != 0);
    }
}

TEST_CASE("DescribeWinInetError: the four TLS codes map to distinct messages") {
    // 12045 invalid CA, 12038 certificate CN mismatch,
    // 12037 certificate date invalid, 12157 secure channel error.
    static const DWORD kTlsCodes[] = { 12045, 12038, 12037, 12157 };
    for (int i = 0; i < 4; i++) {
        CheckDistinctDiagnostic(kTlsCodes[i], kTlsCodes, 4);
    }
}

TEST_CASE("DescribeWinInetError: TLS messages name the code and the cause") {
    const TCHAR* invalidCa = HttpClient::DescribeWinInetError(12045);
    CHECK(invalidCa != NULL);
    // The number has to survive into the message: it is what an operator can
    // look up, and what a bug report can be searched for.
    CHECK(invalidCa && wcsstr(invalidCa, TEXT("12045")) != NULL);

    // The handshake failure is the one that needs to point at the platform
    // ceiling rather than at the network.
    const TCHAR* channel = HttpClient::DescribeWinInetError(12157);
    CHECK(channel != NULL);
    CHECK(channel && wcsstr(channel, TEXT("12157")) != NULL);
    CHECK(channel && wcsstr(channel, TEXT("TLS 1.0")) != NULL);
}

TEST_CASE("DescribeWinInetError: common network codes are described too") {
    static const DWORD kNetCodes[] = { 12002, 12007, 12029, 12030, 12031, 12163 };
    for (int i = 0; i < 6; i++) {
        const TCHAR* text = HttpClient::DescribeWinInetError(kNetCodes[i]);
        CHECK(text != NULL);
        CHECK(text && lstrlen(text) > 0);
    }
}

TEST_CASE("DescribeWinInetError: an unmapped code returns NULL") {
    // NULL is the signal for "no advice"; the caller then reports the raw
    // number instead of inventing an explanation for it.
    CHECK(HttpClient::DescribeWinInetError(0) == NULL);
    CHECK(HttpClient::DescribeWinInetError(12345) == NULL);
    CHECK(HttpClient::DescribeWinInetError(0xFFFFFFFFu) == NULL);
}

// ---------------------------------------------------------------------------
// Certificate relaxation must be opt-in.
// ---------------------------------------------------------------------------

TEST_CASE("SetIgnoreCertificateErrors: defaults to false and is explicit") {
    HttpClient hc;
    // Accepting any certificate from any party is never the default: with the
    // API token riding in the Authorization header, an on-path attacker
    // collects a working credential on the first request.
    CHECK_FALSE(hc.IgnoreCertificateErrors());

    hc.SetIgnoreCertificateErrors(true);
    CHECK(hc.IgnoreCertificateErrors());

    hc.SetIgnoreCertificateErrors(false);
    CHECK_FALSE(hc.IgnoreCertificateErrors());
}
