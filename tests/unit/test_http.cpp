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
