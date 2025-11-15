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

    // Configuration
    void SetBaseUrl(const TCHAR* baseUrl);
    const TCHAR* GetBaseUrl() const;

private:
    HttpClient* m_httpClient;
    TCHAR* m_baseUrl;
    TCHAR* m_authToken;
    bool m_authenticated;

    // Helper methods
    bool MakeApiRequest(const TCHAR* method, const TCHAR* endpoint, const TCHAR* body, TCHAR* response, DWORD maxResponseLen);
    void SetAuthHeaders();
};

} // namespace HBX

#endif // HBCLIENT_HPP
