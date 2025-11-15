#include "../include/HbClient.hpp"
#include <string.h>

namespace HBX {

HbClient::HbClient()
    : m_httpClient(NULL)
    , m_baseUrl(NULL)
    , m_authToken(NULL)
    , m_authenticated(false)
{
    m_httpClient = new HttpClient();
}

HbClient::~HbClient()
{
    if (m_httpClient) {
        delete m_httpClient;
    }
    if (m_baseUrl) {
        delete[] m_baseUrl;
    }
    if (m_authToken) {
        delete[] m_authToken;
    }
}

bool HbClient::Authenticate(const TCHAR* deviceId, const TCHAR* apiKey)
{
    // TODO: Implement authentication
    m_authenticated = true;
    return true;
}

bool HbClient::IsAuthenticated() const
{
    return m_authenticated;
}

void HbClient::Logout()
{
    m_authenticated = false;
    if (m_authToken) {
        delete[] m_authToken;
        m_authToken = NULL;
    }
}

bool HbClient::GetItem(const TCHAR* barcode, Models::Item* item)
{
    // TODO: Implement API call to get item
    return false;
}

bool HbClient::UpdateItemLocation(const TCHAR* barcode, const TCHAR* locationId)
{
    // TODO: Implement API call
    return false;
}

bool HbClient::CreateItem(const Models::Item* item)
{
    // TODO: Implement API call
    return false;
}

bool HbClient::UpdateItem(const Models::Item* item)
{
    // TODO: Implement API call
    return false;
}

bool HbClient::GetLocation(const TCHAR* locationId, Models::Location* location)
{
    // TODO: Implement API call
    return false;
}

bool HbClient::GetAllLocations(Models::Location** locations, int* count)
{
    // TODO: Implement API call
    *count = 0;
    return false;
}

bool HbClient::SyncPendingTransactions()
{
    // TODO: Implement sync
    return false;
}

void HbClient::SetBaseUrl(const TCHAR* baseUrl)
{
    if (m_baseUrl) {
        delete[] m_baseUrl;
    }
    if (baseUrl) {
        int len = lstrlen(baseUrl) + 1;
        m_baseUrl = new TCHAR[len];
        lstrcpy(m_baseUrl, baseUrl);
    } else {
        m_baseUrl = NULL;
    }
}

const TCHAR* HbClient::GetBaseUrl() const
{
    return m_baseUrl;
}

bool HbClient::MakeApiRequest(const TCHAR* method, const TCHAR* endpoint, const TCHAR* body, TCHAR* response, DWORD maxResponseLen)
{
    // TODO: Implement API request
    return false;
}

void HbClient::SetAuthHeaders()
{
    // TODO: Set auth headers on HTTP client
}

} // namespace HBX
