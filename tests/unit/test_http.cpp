// test_http.cpp -- Unit tests for HBX::HttpClient URL parsing and response struct.
//
// These tests exercise only the pure, network-free surface of HttpClient:
//   - ParseUrl(): protocol/host/port/path extraction (no sockets touched).
//   - HttpResponse: default-constructed field values.
//   - GetLastHttpStatusCode()/AddHeader()/ClearHeaders(): trivial config paths.
//
// No Get/Post/Put/Delete calls are made -- no server is available in the host
// build, and those methods open real sockets.

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
