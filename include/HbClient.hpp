#ifndef HBCLIENT_HPP
#define HBCLIENT_HPP

#include <windows.h>
#include "HttpClient.hpp"
#include "Models/Item.hpp"
#include "Models/Location.hpp"

namespace HBX {

/**
 * HomeBox API client
 * High-level interface for communicating with HomeBox backend
 */
class HbClient {
public:
    HbClient();
    ~HbClient();

    // Authentication
    bool Authenticate(const TCHAR* deviceId, const TCHAR* apiKey);
    bool IsAuthenticated() const;
    void Logout();

    /**
     * Bearer token of the current session, or NULL when there is none. The
     * client keeps ownership; the pointer stays valid until the next call that
     * changes the session (Authenticate, SetAuthToken, Logout, or a 401).
     */
    const TCHAR* GetAuthToken() const;

    /**
     * Adopts a token issued earlier - typically one the caller persisted in
     * hb_conf.json - and marks the client authenticated without a round trip.
     * A battery swap ends the process several times a shift, and the restart
     * that lands out of coverage cannot authenticate at all, so the session has
     * to be restorable from storage. `deviceId` may be NULL to keep the one
     * already known; a NULL or empty `token` is a Logout(), since a client that
     * believes in an empty token sends "Bearer " and gets 401 forever.
     */
    void SetAuthToken(const TCHAR* deviceId, const TCHAR* token);

    /**
     * HTTP status of the most recent request that reached the server, or 0 when
     * the last one never got a reply (no route, DNS failure, timeout). This is
     * the only way a caller can tell a rejected session from a missing record:
     * both otherwise surface as a bare false. A 401 additionally drops the
     * session, so IsAuthenticated() reports false and the caller can decide to
     * re-authenticate and retry.
     */
    int GetLastStatusCode() const;

    // Item operations
    bool GetItem(const TCHAR* barcode, Models::Item* item);
    bool UpdateItemLocation(const TCHAR* barcode, const TCHAR* locationId);
    bool CreateItem(const Models::Item* item);
    bool UpdateItem(const Models::Item* item);

    // Location operations
    bool GetLocation(const TCHAR* locationId, Models::Location* location);
    bool GetAllLocations(Models::Location** locations, int* count);

    // Sync operations
    bool SyncPendingTransactions();
    // Post a batch of queued transaction strings to the sync endpoint.
    bool SyncPendingTransactions(const TCHAR* const* transactions, int count);

    // Configuration
    void SetBaseUrl(const TCHAR* baseUrl);
    const TCHAR* GetBaseUrl() const;

    /**
     * Network timeout applied to every subsequent request. The HTTP transport
     * defaults to 30 seconds, which is a frozen screen for the whole of it when
     * a scan lookup runs on the UI thread, so the owner is expected to pick a
     * value suited to the link it is on.
     */
    void SetRequestTimeout(DWORD timeoutMs);

private:
    HttpClient* m_httpClient;
    TCHAR* m_baseUrl;
    TCHAR* m_authToken;
    TCHAR* m_deviceId;
    bool m_authenticated;
    int m_lastStatusCode;

    /**
     * Issues one authenticated API call against m_baseUrl + endpoint.
     *
     * On success *response receives the complete response body as a
     * heap-allocated, NUL-terminated string (never NULL) that the caller must
     * release with delete[]; on failure it is set to NULL. Pass NULL for
     * `response` to discard the body. The body is never truncated to fit a
     * caller buffer -- an API reply that does not fit used to be silently cut
     * short and reported as a success.
     */
    bool MakeApiRequest(const TCHAR* method, const TCHAR* endpoint, const TCHAR* body, TCHAR** response);
    void SetAuthHeaders();

    // Not copyable: the instance owns the HTTP client and its strings.
    HbClient(const HbClient&);
    HbClient& operator=(const HbClient&);
};

} // namespace HBX

#endif // HBCLIENT_HPP
