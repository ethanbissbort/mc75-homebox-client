#include "../include/HbClient.hpp"
#include "../include/Common.hpp"
#include <string.h>
#include <wchar.h>

namespace HBX {

HbClient::HbClient()
    : m_httpClient(NULL)
    , m_baseUrl(NULL)
    , m_authToken(NULL)
    , m_authenticated(false)
    , m_lastError(API_ERROR_NONE)
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
    if (!deviceId || !apiKey) {
        return false;
    }

    // Build authentication request body
    TCHAR requestBody[1024];
    wsprintf(requestBody,
        TEXT("{\"deviceId\":\"%s\",\"apiKey\":\"%s\"}"),
        deviceId, apiKey);

    // Make authentication request
    TCHAR response[4096];
    bool success = MakeApiRequest(TEXT("POST"), TEXT("/api/v1/auth/device"), requestBody, response, sizeof(response) / sizeof(TCHAR));

    if (!success) {
        m_authenticated = false;
        return false;
    }

    // Parse token from response
    // Expected format: {"token": "..."}
    const TCHAR* tokenStart = wcsstr(response, TEXT("\"token\""));
    if (!tokenStart) {
        m_authenticated = false;
        return false;
    }

    // Find the token value
    tokenStart = wcsstr(tokenStart, TEXT(":"));
    if (!tokenStart) {
        m_authenticated = false;
        return false;
    }

    // Skip whitespace and opening quote
    tokenStart++;
    while (*tokenStart == ' ' || *tokenStart == '\t') {
        tokenStart++;
    }
    if (*tokenStart == '"') {
        tokenStart++;
    }

    // Find closing quote
    const TCHAR* tokenEnd = wcsstr(tokenStart, TEXT("\""));
    if (!tokenEnd) {
        m_authenticated = false;
        return false;
    }

    // Extract token
    int tokenLen = tokenEnd - tokenStart;
    if (m_authToken) {
        delete[] m_authToken;
    }
    m_authToken = new TCHAR[tokenLen + 1];
    for (int i = 0; i < tokenLen; i++) {
        m_authToken[i] = tokenStart[i];
    }
    m_authToken[tokenLen] = '\0';

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
    if (!barcode || !item) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    wsprintf(endpoint, TEXT("/api/v1/items/%s"), barcode);

    // Make GET request
    TCHAR response[8192];
    bool success = MakeApiRequest(TEXT("GET"), endpoint, NULL, response, sizeof(response) / sizeof(TCHAR));

    if (!success) {
        return false;
    }

    // Parse JSON response into Item object
    return item->FromJson(response);
}

bool HbClient::UpdateItemLocation(const TCHAR* barcode, const TCHAR* locationId)
{
    if (!barcode || !locationId) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    wsprintf(endpoint, TEXT("/api/v1/items/%s/location"), barcode);

    // Build request body
    TCHAR requestBody[512];
    wsprintf(requestBody, TEXT("{\"locationId\":\"%s\"}"), locationId);

    // Make PATCH request (using PUT as fallback)
    TCHAR response[4096];
    return MakeApiRequest(TEXT("PUT"), endpoint, requestBody, response, sizeof(response) / sizeof(TCHAR));
}

bool HbClient::CreateItem(const Models::Item* item)
{
    if (!item || !item->IsValid()) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Serialize item to JSON
    TCHAR* requestBody = item->ToJson();
    if (!requestBody) {
        return false;
    }

    // Make POST request
    TCHAR response[4096];
    bool success = MakeApiRequest(TEXT("POST"), TEXT("/api/v1/items"), requestBody, response, sizeof(response) / sizeof(TCHAR));

    // Cleanup
    delete[] requestBody;

    return success;
}

bool HbClient::UpdateItem(const Models::Item* item)
{
    if (!item || !item->IsValid() || !item->GetId()) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    wsprintf(endpoint, TEXT("/api/v1/items/%s"), item->GetId());

    // Serialize item to JSON
    TCHAR* requestBody = item->ToJson();
    if (!requestBody) {
        return false;
    }

    // Make PUT request
    TCHAR response[4096];
    bool success = MakeApiRequest(TEXT("PUT"), endpoint, requestBody, response, sizeof(response) / sizeof(TCHAR));

    // Cleanup
    delete[] requestBody;

    return success;
}

bool HbClient::GetLocation(const TCHAR* locationId, Models::Location* location)
{
    if (!locationId || !location) {
        return false;
    }

    if (!m_authenticated) {
        return false;
    }

    // Build endpoint URL
    TCHAR endpoint[512];
    wsprintf(endpoint, TEXT("/api/v1/locations/%s"), locationId);

    // Make GET request
    TCHAR response[8192];
    bool success = MakeApiRequest(TEXT("GET"), endpoint, NULL, response, sizeof(response) / sizeof(TCHAR));

    if (!success) {
        return false;
    }

    // Parse JSON response into Location object
    return location->FromJson(response);
}

bool HbClient::GetAllLocations(Models::Location** locations, int* count)
{
    if (!locations || !count) {
        return false;
    }

    *locations = NULL;
    *count = 0;

    if (!m_authenticated) {
        return false;
    }

    // Make GET request
    TCHAR response[16384]; // Larger buffer for array response
    bool success = MakeApiRequest(TEXT("GET"), TEXT("/api/v1/locations"), NULL, response, sizeof(response) / sizeof(TCHAR));

    if (!success) {
        return false;
    }

    // Parse JSON array response
    // Expected format: [{"id":"1",...}, {"id":"2",...}]

    // Count number of locations in array (count opening braces after first '[')
    int locationCount = 0;
    const TCHAR* ptr = response;
    bool inArray = false;
    while (*ptr) {
        if (*ptr == '[') {
            inArray = true;
        } else if (inArray && *ptr == '{') {
            locationCount++;
        }
        ptr++;
    }

    if (locationCount == 0) {
        return true; // Empty array is valid
    }

    // Allocate array for locations
    Models::Location* locArray = new Models::Location[locationCount];
    int currentLoc = 0;

    // Parse each location object
    ptr = wcsstr(response, TEXT("["));
    if (ptr) {
        ptr++; // Skip '['

        while (*ptr && currentLoc < locationCount) {
            // Find start of object
            const TCHAR* objStart = wcsstr(ptr, TEXT("{"));
            if (!objStart) break;

            // Find end of object (simple approach - find matching '}')
            const TCHAR* objEnd = objStart;
            int braceCount = 0;
            while (*objEnd) {
                if (*objEnd == '{') braceCount++;
                if (*objEnd == '}') {
                    braceCount--;
                    if (braceCount == 0) break;
                }
                objEnd++;
            }

            if (*objEnd == '}') {
                // Extract object JSON
                int objLen = objEnd - objStart + 1;
                TCHAR* objJson = new TCHAR[objLen + 1];
                for (int i = 0; i < objLen; i++) {
                    objJson[i] = objStart[i];
                }
                objJson[objLen] = '\0';

                // Parse into location object
                locArray[currentLoc].FromJson(objJson);
                delete[] objJson;

                currentLoc++;
                ptr = objEnd + 1;
            } else {
                break;
            }
        }
    }

    *locations = locArray;
    *count = currentLoc;

    return true;
}

bool HbClient::SyncPendingTransactions()
{
    if (!m_authenticated) {
        return false;
    }

    // Make POST request to sync endpoint
    // The backend will expect a batch of transactions
    TCHAR requestBody[4096];
    wsprintf(requestBody, TEXT("{\"deviceId\":\"%s\"}"), TEXT("DEVICE_ID")); // Placeholder

    TCHAR response[8192];
    bool success = MakeApiRequest(TEXT("POST"), TEXT("/api/v1/sync"), requestBody, response, sizeof(response) / sizeof(TCHAR));

    return success;
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
    if (!m_httpClient || !m_baseUrl || !method || !endpoint) {
        return false;
    }

    // Build full URL
    TCHAR fullUrl[1024];
    wsprintf(fullUrl, TEXT("%s%s"), m_baseUrl, endpoint);

    // Set authentication headers
    SetAuthHeaders();

    // Make HTTP request
    HttpResponse httpResponse;
    bool success = false;

    if (lstrcmp(method, TEXT("GET")) == 0) {
        success = m_httpClient->Get(fullUrl, &httpResponse);
    } else if (lstrcmp(method, TEXT("POST")) == 0) {
        success = m_httpClient->Post(fullUrl, body, &httpResponse);
    } else if (lstrcmp(method, TEXT("PUT")) == 0) {
        success = m_httpClient->Put(fullUrl, body, &httpResponse);
    } else if (lstrcmp(method, TEXT("DELETE")) == 0) {
        success = m_httpClient->Delete(fullUrl, &httpResponse);
    }

    if (!success) {
        return false;
    }

    // Check status code (200-299 is success)
    if (httpResponse.statusCode < 200 || httpResponse.statusCode >= 300) {
        if (httpResponse.body) {
            delete[] httpResponse.body;
        }
        return false;
    }

    // Copy response body to output buffer
    if (httpResponse.body && response && maxResponseLen > 0) {
        int len = lstrlen(httpResponse.body);
        if (len >= (int)maxResponseLen) {
            len = maxResponseLen - 1;
        }
        for (int i = 0; i < len; i++) {
            response[i] = httpResponse.body[i];
        }
        response[len] = '\0';
    }

    // Cleanup
    if (httpResponse.body) {
        delete[] httpResponse.body;
    }

    return true;
}

void HbClient::SetAuthHeaders()
{
    if (!m_httpClient) {
        return;
    }

    // Clear existing headers
    m_httpClient->ClearHeaders();

    // Set standard headers
    m_httpClient->AddHeader(TEXT("Content-Type"), TEXT("application/json"));
    m_httpClient->AddHeader(TEXT("Accept"), TEXT("application/json"));

    // Set authorization header if authenticated
    if (m_authToken) {
        TCHAR authHeader[512];
        wsprintf(authHeader, TEXT("Bearer %s"), m_authToken);
        m_httpClient->AddHeader(TEXT("Authorization"), authHeader);
    }
}

} // namespace HBX

ApiError HbClient::GetLastError() const
{
    return m_lastError;
}

const TCHAR* HbClient::BuildFullUrl(const TCHAR* endpoint) const
{
    static TCHAR fullUrl[Common::MAX_URL_LENGTH];

    if (m_baseUrl) {
        wsprintf(fullUrl, TEXT("%s%s"), m_baseUrl, endpoint);
    } else {
        lstrcpy(fullUrl, endpoint);
    }

    return fullUrl;
}
} // namespace HBX
