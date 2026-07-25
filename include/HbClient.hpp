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

private:
    HttpClient* m_httpClient;
    TCHAR* m_baseUrl;
    TCHAR* m_authToken;
    TCHAR* m_deviceId;
    bool m_authenticated;

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
