# 🌐 API Integration Notes

> **Complete HomeBox API reference for MC75 Client integration**

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Authentication](#-authentication)
- [API Endpoints](#-api-endpoints)
- [Request/Response Format](#-requestresponse-format)
- [Error Handling](#-error-handling)
- [Rate Limiting](#-rate-limiting)
- [Offline Synchronization](#-offline-synchronization)
- [Best Practices](#-best-practices)
- [Examples](#-examples)

---

## 🎯 Overview

The **HomeBox API** is a RESTful HTTP API that provides inventory management services. The MC75 client communicates with this API to perform item lookups, location updates, and transaction synchronization.

### 📡 API Characteristics

| Feature | Details |
|---------|---------|
| **Protocol** | HTTPS (recommended) or HTTP |
| **Architecture** | RESTful |
| **Data Format** | JSON (application/json) |
| **Authentication** | Bearer Token (JWT) |
| **Versioning** | URL path (`/api/v1/...`) |
| **Character Encoding** | UTF-8 |

### 🔗 Base URL Configuration

The base URL is configured in `hb_conf.json`:

```json
{
  "apiBaseUrl": "https://api.homebox.example.com",
  "deviceId": "MC75-WAREHOUSE-001",
  "apiKey": "your-api-key-here"
}
```

**Environment Examples**:
- Production: `https://api.homebox.example.com`
- Staging: `https://staging-api.homebox.example.com`
- Development: `http://localhost:8080`
- Local Testing: `http://192.168.1.100:8080`

---

## 🔐 Authentication

### Authentication Flow

```
┌──────────────┐
│   Device     │
│   Startup    │
└──────┬───────┘
       │
       ▼
┌──────────────────────────────────┐
│ POST /api/v1/auth/device         │
│                                  │
│ Body:                            │
│ {                                │
│   "deviceId": "MC75-001",        │
│   "apiKey": "sk_abc123..."       │
│ }                                │
└──────┬───────────────────────────┘
       │
       ▼
┌──────────────────────────────────┐
│ Response: 200 OK                 │
│                                  │
│ {                                │
│   "token": "eyJhbGc...",         │
│   "expiresAt": "2025-11-16...",  │
│   "deviceName": "MC75-001"       │
│ }                                │
└──────┬───────────────────────────┘
       │
       ▼
┌──────────────────────────────────┐
│ Store token in memory            │
│ m_authToken = "eyJhbGc..."       │
└──────┬───────────────────────────┘
       │
       ▼
┌──────────────────────────────────┐
│ All subsequent requests include: │
│ Authorization: Bearer eyJhbGc... │
└──────────────────────────────────┘
```

### Device Authentication

**Endpoint**: `POST /api/v1/auth/device`

**Purpose**: Authenticate a handheld device and obtain a bearer token

**Request Headers**:
```http
Content-Type: application/json
Accept: application/json
```

**Request Body**:
```json
{
  "deviceId": "MC75-WAREHOUSE-001",
  "apiKey": "sk_abc123def456ghi789"
}
```

**Response (Success - 200 OK)**:
```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expiresAt": "2025-11-16T14:30:00Z",
  "deviceName": "MC75-WAREHOUSE-001",
  "permissions": ["scan", "update_location", "sync"]
}
```

**Response (Failure - 401 Unauthorized)**:
```json
{
  "error": "unauthorized",
  "message": "Invalid device ID or API key"
}
```

### Using Bearer Token

All authenticated requests must include the token:

```http
GET /api/v1/items/1234567890 HTTP/1.1
Host: api.homebox.example.com
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
Accept: application/json
```

**Token Lifecycle**:
1. Obtain token on application startup
2. Store in memory (`m_authToken`)
3. Include in all API requests
4. Token typically valid for 24 hours
5. Re-authenticate if token expires (401 response)

---

## 📚 API Endpoints

### 1️⃣ Items API

#### Get Item by Barcode

**Endpoint**: `GET /api/v1/items/{barcode}`

**Purpose**: Retrieve item details by scanning barcode

**URL Parameters**:
- `barcode` (string, required): Item barcode (UPC/EAN/Code128)

**Request**:
```http
GET /api/v1/items/1234567890 HTTP/1.1
Host: api.homebox.example.com
Authorization: Bearer eyJhbGc...
Accept: application/json
```

**Response (Success - 200 OK)**:
```json
{
  "id": "item_abc123",
  "barcode": "1234567890",
  "name": "Widget A - Premium",
  "description": "High-quality widget for industrial use",
  "locationId": "loc_warehouse_a01",
  "category": "Electronics",
  "quantity": 150,
  "createdAt": "2025-01-15T10:30:00Z",
  "updatedAt": "2025-11-15T14:22:15Z"
}
```

**Response (Not Found - 404 Not Found)**:
```json
{
  "error": "not_found",
  "message": "Item with barcode 1234567890 not found"
}
```

**Client Implementation**:
```cpp
bool HbClient::GetItem(const TCHAR* barcode, Models::Item* item) {
    TCHAR endpoint[512];
    wsprintf(endpoint, TEXT("/api/v1/items/%s"), barcode);

    TCHAR response[8192];
    bool success = MakeApiRequest(TEXT("GET"), endpoint, NULL,
                                   response, sizeof(response) / sizeof(TCHAR));

    if (!success) return false;

    return item->FromJson(response);
}
```

---

#### Create Item

**Endpoint**: `POST /api/v1/items`

**Purpose**: Create a new item in the inventory

**Request Headers**:
```http
Content-Type: application/json
Authorization: Bearer eyJhbGc...
```

**Request Body**:
```json
{
  "barcode": "9876543210",
  "name": "Widget B - Standard",
  "description": "Standard widget for general use",
  "locationId": "loc_warehouse_b02",
  "category": "Hardware",
  "quantity": 75
}
```

**Response (Success - 201 Created)**:
```json
{
  "id": "item_def456",
  "barcode": "9876543210",
  "name": "Widget B - Standard",
  "description": "Standard widget for general use",
  "locationId": "loc_warehouse_b02",
  "category": "Hardware",
  "quantity": 75,
  "createdAt": "2025-11-15T15:30:00Z",
  "updatedAt": "2025-11-15T15:30:00Z"
}
```

**Response (Validation Error - 400 Bad Request)**:
```json
{
  "error": "validation_error",
  "message": "Invalid item data",
  "details": {
    "barcode": "Barcode must be 10-14 digits",
    "quantity": "Quantity must be non-negative"
  }
}
```

---

#### Update Item

**Endpoint**: `PUT /api/v1/items/{id}`

**Purpose**: Update an existing item's details

**URL Parameters**:
- `id` (string, required): Item ID (not barcode)

**Request Body**:
```json
{
  "name": "Widget A - Premium (Updated)",
  "description": "Updated description",
  "quantity": 200,
  "category": "Electronics - Premium"
}
```

**Response (Success - 200 OK)**:
```json
{
  "id": "item_abc123",
  "barcode": "1234567890",
  "name": "Widget A - Premium (Updated)",
  "description": "Updated description",
  "locationId": "loc_warehouse_a01",
  "category": "Electronics - Premium",
  "quantity": 200,
  "createdAt": "2025-01-15T10:30:00Z",
  "updatedAt": "2025-11-15T16:45:00Z"
}
```

---

#### Update Item Location

**Endpoint**: `PUT /api/v1/items/{barcode}/location`

**Purpose**: Move an item to a different location

**URL Parameters**:
- `barcode` (string, required): Item barcode

**Request Body**:
```json
{
  "locationId": "loc_warehouse_c03"
}
```

**Response (Success - 200 OK)**:
```json
{
  "id": "item_abc123",
  "barcode": "1234567890",
  "locationId": "loc_warehouse_c03",
  "updatedAt": "2025-11-15T17:00:00Z"
}
```

**Client Implementation**:
```cpp
bool HbClient::UpdateItemLocation(const TCHAR* barcode, const TCHAR* locationId) {
    TCHAR endpoint[512];
    wsprintf(endpoint, TEXT("/api/v1/items/%s/location"), barcode);

    TCHAR requestBody[512];
    wsprintf(requestBody, TEXT("{\"locationId\":\"%s\"}"), locationId);

    TCHAR response[4096];
    return MakeApiRequest(TEXT("PUT"), endpoint, requestBody,
                          response, sizeof(response) / sizeof(TCHAR));
}
```

---

### 2️⃣ Locations API

#### Get Location by ID

**Endpoint**: `GET /api/v1/locations/{id}`

**Purpose**: Retrieve location details

**URL Parameters**:
- `id` (string, required): Location ID

**Request**:
```http
GET /api/v1/locations/loc_warehouse_a01 HTTP/1.1
Host: api.homebox.example.com
Authorization: Bearer eyJhbGc...
Accept: application/json
```

**Response (Success - 200 OK)**:
```json
{
  "id": "loc_warehouse_a01",
  "name": "Warehouse A - Aisle 01",
  "description": "Electronics storage area",
  "parentId": "loc_warehouse_a",
  "createdAt": "2025-01-01T00:00:00Z"
}
```

---

#### Get All Locations

**Endpoint**: `GET /api/v1/locations`

**Purpose**: Retrieve all available locations

**Query Parameters** (optional):
- `page` (integer): Page number for pagination (default: 1)
- `limit` (integer): Items per page (default: 100, max: 500)

**Request**:
```http
GET /api/v1/locations?page=1&limit=100 HTTP/1.1
Host: api.homebox.example.com
Authorization: Bearer eyJhbGc...
Accept: application/json
```

**Response (Success - 200 OK)**:
```json
{
  "locations": [
    {
      "id": "loc_warehouse_a01",
      "name": "Warehouse A - Aisle 01",
      "description": "Electronics storage",
      "parentId": "loc_warehouse_a"
    },
    {
      "id": "loc_warehouse_a02",
      "name": "Warehouse A - Aisle 02",
      "description": "Hardware storage",
      "parentId": "loc_warehouse_a"
    }
  ],
  "pagination": {
    "page": 1,
    "limit": 100,
    "total": 2,
    "pages": 1
  }
}
```

**Client Implementation**:
```cpp
bool HbClient::GetAllLocations(Models::Location** locations, int* count) {
    TCHAR response[16384];
    bool success = MakeApiRequest(TEXT("GET"), TEXT("/api/v1/locations"),
                                   NULL, response, sizeof(response) / sizeof(TCHAR));

    if (!success) return false;

    // Parse JSON array and allocate location array
    // (See HbClient.cpp for full implementation)
    return true;
}
```

---

### 3️⃣ Synchronization API

#### Sync Pending Transactions

**Endpoint**: `POST /api/v1/sync`

**Purpose**: Batch synchronize queued offline transactions

**Request Body**:
```json
{
  "deviceId": "MC75-WAREHOUSE-001",
  "transactions": [
    {
      "transactionId": "trans_001",
      "type": "SCAN",
      "barcode": "1234567890",
      "timestamp": "2025-11-15T14:30:15Z"
    },
    {
      "transactionId": "trans_002",
      "type": "UPDATE_LOCATION",
      "barcode": "9876543210",
      "locationId": "loc_warehouse_b02",
      "timestamp": "2025-11-15T14:35:22Z"
    }
  ]
}
```

**Response (Success - 200 OK)**:
```json
{
  "syncedCount": 2,
  "failedCount": 0,
  "results": [
    {
      "transactionId": "trans_001",
      "status": "success"
    },
    {
      "transactionId": "trans_002",
      "status": "success"
    }
  ]
}
```

**Response (Partial Success - 207 Multi-Status)**:
```json
{
  "syncedCount": 1,
  "failedCount": 1,
  "results": [
    {
      "transactionId": "trans_001",
      "status": "success"
    },
    {
      "transactionId": "trans_002",
      "status": "failed",
      "error": "Item not found"
    }
  ]
}
```

---

## 📦 Request/Response Format

### Request Headers

**Standard Headers**:
```http
Content-Type: application/json
Accept: application/json
Authorization: Bearer {token}
User-Agent: HBXClient/1.0 (Windows Mobile 6.5)
```

**Optional Headers**:
```http
X-Device-ID: MC75-WAREHOUSE-001
X-Request-ID: req_12345
X-Client-Version: 1.0.0
```

### Response Structure

#### Success Response

**Structure**:
```json
{
  // Direct data object or array
  "id": "...",
  "name": "...",
  // ... other fields
}
```

**HTTP Status Codes**:
- `200 OK`: Request successful
- `201 Created`: Resource created successfully
- `204 No Content`: Successful, no response body

#### Error Response

**Structure**:
```json
{
  "error": "error_code",
  "message": "Human-readable error message",
  "details": {
    // Optional additional error details
  }
}
```

**HTTP Status Codes**:
- `400 Bad Request`: Invalid request data
- `401 Unauthorized`: Missing or invalid authentication
- `403 Forbidden`: Insufficient permissions
- `404 Not Found`: Resource not found
- `409 Conflict`: Resource conflict (e.g., duplicate barcode)
- `422 Unprocessable Entity`: Validation failed
- `429 Too Many Requests`: Rate limit exceeded
- `500 Internal Server Error`: Server error
- `503 Service Unavailable`: Temporary outage

### JSON Data Types

| Type | Example | Usage |
|------|---------|-------|
| **String** | `"Widget A"` | Names, descriptions, IDs |
| **Number** | `150` | Quantities, counts |
| **Boolean** | `true` | Flags, settings |
| **Null** | `null` | Optional missing values |
| **Object** | `{"id": "..."}` | Nested structures |
| **Array** | `[1, 2, 3]` | Lists, collections |

**Date/Time Format**: ISO 8601 UTC
```
"2025-11-15T14:30:15Z"
```

---

## ⚠️ Error Handling

### Common Error Codes

| Code | HTTP Status | Description | Client Action |
|------|-------------|-------------|---------------|
| `unauthorized` | 401 | Invalid/expired token | Re-authenticate |
| `forbidden` | 403 | Insufficient permissions | Notify user, log error |
| `not_found` | 404 | Resource doesn't exist | Queue for offline sync |
| `validation_error` | 400 | Invalid request data | Show validation errors |
| `conflict` | 409 | Duplicate resource | Notify user |
| `rate_limited` | 429 | Too many requests | Retry with backoff |
| `server_error` | 500 | Server malfunction | Queue for retry |
| `unavailable` | 503 | Service down | Switch to offline mode |

### Error Handling Strategy

```cpp
bool success = MakeApiRequest(method, endpoint, body, response, maxLen);

if (!success) {
    // Check HTTP status code
    if (httpResponse.statusCode == 401) {
        // Token expired - re-authenticate
        Authenticate(deviceId, apiKey);
        // Retry request
    } else if (httpResponse.statusCode == 404) {
        // Item not found
        if (!IsOnline()) {
            // Queue for offline sync
            syncEngine->QueueTransaction(type, data);
        } else {
            // Show "not found" error
            MessageBox(hwnd, TEXT("Item not found"), ...);
        }
    } else if (httpResponse.statusCode >= 500) {
        // Server error - queue for retry
        syncEngine->QueueTransaction(type, data);
        journal->LogError("SERVER_ERROR", "Server returned 500");
    }

    return false;
}
```

### Retry Logic

**Exponential Backoff**:
```
Attempt 1: Immediate
Attempt 2: Wait 1 second
Attempt 3: Wait 2 seconds
Attempt 4: Wait 4 seconds
Attempt 5: Wait 8 seconds
Give up after 5 attempts
```

**Retryable Errors**:
- `429 Too Many Requests`
- `500 Internal Server Error`
- `502 Bad Gateway`
- `503 Service Unavailable`
- Network timeouts
- Connection failures

**Non-Retryable Errors**:
- `400 Bad Request`
- `401 Unauthorized` (without re-auth)
- `403 Forbidden`
- `404 Not Found`
- `422 Unprocessable Entity`

---

## 🚦 Rate Limiting

### Rate Limit Headers

The API includes rate limit information in response headers:

```http
HTTP/1.1 200 OK
X-RateLimit-Limit: 1000
X-RateLimit-Remaining: 950
X-RateLimit-Reset: 1700062800
```

**Header Definitions**:
- `X-RateLimit-Limit`: Maximum requests per window
- `X-RateLimit-Remaining`: Requests remaining in current window
- `X-RateLimit-Reset`: Unix timestamp when limit resets

### Rate Limit Policy

| Endpoint | Limit | Window | Notes |
|----------|-------|--------|-------|
| `POST /api/v1/auth/device` | 10 | 1 hour | Authentication attempts |
| `GET /api/v1/items/*` | 1000 | 1 hour | Item lookups |
| `POST /api/v1/items` | 100 | 1 hour | Item creation |
| `PUT /api/v1/items/*` | 500 | 1 hour | Item updates |
| `POST /api/v1/sync` | 60 | 1 hour | Batch sync operations |

### Handling Rate Limits

**Response (429 Too Many Requests)**:
```json
{
  "error": "rate_limited",
  "message": "Rate limit exceeded. Try again in 3600 seconds.",
  "retryAfter": 3600
}
```

**Client Strategy**:
1. Check `X-RateLimit-Remaining` header
2. If low (<10), slow down requests
3. If rate limited (429), queue transaction for offline sync
4. Respect `retryAfter` value before retrying

---

## 🔄 Offline Synchronization

### Offline Transaction Queue

When the device is offline or API calls fail, transactions are queued locally.

**Queue Entry Format** (in Journal):
```
TRANS|SCAN|1234567890|2025-11-15T14:30:15
TRANS|UPDATE_LOCATION|{"barcode":"9876543210","locationId":"loc_b02"}|2025-11-15T14:35:22
```

### Sync Process

```
1. User clicks "Sync" or auto-sync timer fires
   │
   ▼
2. Check connectivity (DNS lookup)
   ├─ Offline → Abort, show "Offline" message
   └─ Online → Continue
   │
   ▼
3. Read all TRANS entries from Journal
   │
   ▼
4. For each transaction:
   ├─ Parse type and data
   ├─ Make API call
   ├─ If success:
   │  ├─ Mark as SYNCED in Journal
   │  └─ Remove from queue
   └─ If failure:
      └─ Keep in queue, log error
   │
   ▼
5. Update sync status:
   ├─ All succeeded → SYNC_SUCCESS
   ├─ Some failed → SYNC_PARTIAL
   └─ All failed → SYNC_FAILED
   │
   ▼
6. Update UI with results
```

### Connectivity Detection

```cpp
bool SyncEngine::IsOnline() const {
    // Try DNS lookup of API hostname
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    struct hostent* host = gethostbyname("api.homebox.example.com");

    WSACleanup();

    return (host != NULL);
}
```

**Caching**: Result cached for 60 seconds to avoid overhead.

---

## ✅ Best Practices

### 1️⃣ Always Use HTTPS in Production

```json
// Production
{
  "apiBaseUrl": "https://api.homebox.example.com"
}

// Development only
{
  "apiBaseUrl": "http://localhost:8080"
}
```

**Security Risks of HTTP**:
- ❌ Credentials transmitted in plaintext
- ❌ Tokens visible to network sniffers
- ❌ Man-in-the-middle attacks possible

---

### 2️⃣ Handle Token Expiration

```cpp
bool MakeApiRequest(...) {
    bool success = httpClient->Get(url, &response);

    if (!success && response.statusCode == 401) {
        // Token expired - re-authenticate
        if (Authenticate(deviceId, apiKey)) {
            // Retry request with new token
            success = httpClient->Get(url, &response);
        }
    }

    return success;
}
```

---

### 3️⃣ Validate API Responses

```cpp
bool Item::FromJson(const TCHAR* json) {
    if (!json || lstrlen(json) == 0) {
        return false;  // Invalid JSON
    }

    // Parse required fields
    if (!ParseStringField(json, TEXT("id"), &m_id)) {
        return false;  // Missing required field
    }

    if (!ParseStringField(json, TEXT("barcode"), &m_barcode)) {
        return false;  // Missing required field
    }

    return IsValid();
}
```

---

### 4️⃣ Queue Failed Requests

```cpp
bool success = hbClient->UpdateItemLocation(barcode, locationId);

if (!success) {
    // Queue for offline sync
    TCHAR transactionData[512];
    wsprintf(transactionData, TEXT("{\"barcode\":\"%s\",\"locationId\":\"%s\"}"),
             barcode, locationId);

    syncEngine->QueueTransaction(TEXT("UPDATE_LOCATION"), transactionData);

    journal->LogInfo(TEXT("Queued location update for offline sync"));
}
```

---

### 5️⃣ Log All API Interactions

```cpp
journal->LogInfo(TEXT("API Request: GET /api/v1/items/1234567890"));

bool success = hbClient->GetItem(barcode, &item);

if (success) {
    journal->LogInfo(TEXT("API Response: 200 OK - Item found"));
} else {
    journal->LogError(TEXT("API_CALL"), TEXT("GetItem failed - see details"));
}
```

---

### 6️⃣ Respect Rate Limits

```cpp
// Track request count
static int requestCount = 0;
static DWORD lastResetTime = GetTickCount();

bool MakeApiRequest(...) {
    // Reset counter every hour
    if (GetTickCount() - lastResetTime > 3600000) {
        requestCount = 0;
        lastResetTime = GetTickCount();
    }

    // Check if approaching limit
    if (requestCount >= 950) {
        // Slow down - queue instead
        syncEngine->QueueTransaction(type, data);
        return false;
    }

    requestCount++;
    return httpClient->Get(url, &response);
}
```

---

## 📘 Examples

### Example 1: Item Lookup Flow

```cpp
// 1. User scans barcode
const TCHAR* barcode = TEXT("1234567890");

// 2. Attempt API lookup
Models::Item item;
bool success = hbClient->GetItem(barcode, &item);

if (success && item.IsValid()) {
    // 3. Item found - display details
    TCHAR message[512];
    wsprintf(message,
        TEXT("Item: %s\nLocation: %s\nQuantity: %d"),
        item.GetName(),
        item.GetLocationId(),
        item.GetQuantity());

    MessageBox(hwnd, message, TEXT("Item Details"), MB_OK);

    // 4. Log transaction
    journal->LogTransaction(TEXT("SCAN"), barcode, TEXT("Item lookup successful"));

} else {
    // 5. Item not found or offline
    if (!syncEngine->IsOnline()) {
        // Queue for offline sync
        TCHAR transData[256];
        wsprintf(transData, TEXT("SCAN:%s"), barcode);

        syncEngine->QueueTransaction(TEXT("ITEM_SCAN"), transData);

        MessageBox(hwnd,
            TEXT("Device offline. Scan queued for synchronization."),
            TEXT("Offline Mode"), MB_OK);
    } else {
        // Online but not found
        MessageBox(hwnd,
            TEXT("Item not found in database."),
            TEXT("Not Found"), MB_OK);
    }

    journal->LogInfo(TEXT("Item lookup failed - queued or not found"));
}
```

---

### Example 2: Location Update

```cpp
// 1. Get current item
Models::Item item;
bool success = hbClient->GetItem(TEXT("1234567890"), &item);

if (!success) {
    MessageBox(hwnd, TEXT("Item not found"), TEXT("Error"), MB_OK);
    return;
}

// 2. Show current location
TCHAR message[256];
wsprintf(message, TEXT("Current location: %s\nMove to: B-02?"),
         item.GetLocationId());

int result = MessageBox(hwnd, message, TEXT("Move Item"), MB_YESNO);

if (result == IDYES) {
    // 3. Update location
    success = hbClient->UpdateItemLocation(TEXT("1234567890"), TEXT("loc_warehouse_b02"));

    if (success) {
        MessageBox(hwnd, TEXT("Location updated"), TEXT("Success"), MB_OK);
        journal->LogTransaction(TEXT("UPDATE_LOCATION"),
                                TEXT("1234567890"),
                                TEXT("Moved to B-02"));
    } else {
        // 4. Failed - queue for sync
        TCHAR transData[512];
        wsprintf(transData,
            TEXT("{\"barcode\":\"1234567890\",\"locationId\":\"loc_warehouse_b02\"}"));

        syncEngine->QueueTransaction(TEXT("UPDATE_LOCATION"), transData);

        MessageBox(hwnd, TEXT("Queued for sync"), TEXT("Offline"), MB_OK);
    }
}
```

---

### Example 3: Batch Sync

```cpp
// User clicks "Sync" button
void OnSyncButtonClick() {
    // 1. Check connectivity
    if (!syncEngine->IsOnline()) {
        MessageBox(hwnd,
            TEXT("Device is offline. Please connect to network."),
            TEXT("Offline"), MB_OK);
        return;
    }

    // 2. Get queued transaction count
    int queuedCount = syncEngine->GetQueuedTransactionCount();

    if (queuedCount == 0) {
        MessageBox(hwnd, TEXT("No pending transactions"), TEXT("Sync"), MB_OK);
        return;
    }

    // 3. Show progress message
    TCHAR message[128];
    wsprintf(message, TEXT("Syncing %d transactions..."), queuedCount);
    MessageBox(hwnd, message, TEXT("Sync"), MB_OK);

    // 4. Perform sync
    bool success = syncEngine->Sync();

    // 5. Show results
    if (success) {
        SyncEngine::SyncStatus status = syncEngine->GetSyncStatus();

        if (status == SyncEngine::SYNC_SUCCESS) {
            MessageBox(hwnd,
                TEXT("All transactions synced successfully"),
                TEXT("Sync Complete"), MB_OK);
        } else if (status == SyncEngine::SYNC_PARTIAL) {
            wsprintf(message,
                TEXT("Some transactions failed. %d remain in queue."),
                syncEngine->GetQueuedTransactionCount());
            MessageBox(hwnd, message, TEXT("Partial Sync"), MB_OK);
        }
    } else {
        MessageBox(hwnd,
            TEXT("Sync failed. Transactions remain queued."),
            TEXT("Sync Failed"), MB_OK);
    }

    journal->LogInfo(TEXT("Manual sync completed"));
}
```

---

## 🔍 Debugging API Issues

### Enable Verbose Logging

```json
{
  "apiBaseUrl": "https://api.homebox.example.com",
  "logLevel": "DEBUG"
}
```

### Check Journal for Errors

```
\Program Files\HBXClient\hbx.journal
```

**Look for**:
```
[2025-11-15 14:30:15] ERROR [API_CALL] Connection timeout
[2025-11-15 14:30:20] ERROR [HTTP_CLIENT] DNS lookup failed
[2025-11-15 14:30:25] ERROR [AUTH] Token expired
```

### Test API with curl

```bash
# Test authentication
curl -X POST https://api.homebox.example.com/api/v1/auth/device \
  -H "Content-Type: application/json" \
  -d '{"deviceId":"TEST","apiKey":"sk_test123"}'

# Test item lookup
curl -X GET https://api.homebox.example.com/api/v1/items/1234567890 \
  -H "Authorization: Bearer YOUR_TOKEN"
```

---

## 📊 API Versioning

### Current Version: v1

**Base Path**: `/api/v1/`

**Version Strategy**: URL path versioning

**Future Versions**:
- v2 will use `/api/v2/` path
- v1 maintained for backward compatibility
- Deprecation notices sent 6 months before sunset

---

<div align="center">

**🌐 Seamless API Integration for Reliable Inventory Management**

[← Back to Design](DESIGN.md) | [Back to README](../README.md) | [Next: Build →](BUILD.md)

</div>
