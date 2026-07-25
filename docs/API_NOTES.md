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
| **Protocol** | HTTPS (device build, WinInet) or HTTP (both transports) |
| **Architecture** | RESTful |
| **Data Format** | JSON (`application/json; charset=utf-8`) |
| **Authentication** | Bearer Token |
| **Versioning** | URL path (`/api/v1/...`) |
| **Character Encoding** | **UTF-8, both directions, on both transports** |

### 🔡 Encoding and Framing (what the client actually does)

These are properties of `src/HttpClient.cpp`, so they hold regardless of which
transport a build uses:

- **Request bodies are encoded to UTF-8** before they go on the wire, and
  `Content-Length` counts **wire bytes**, not characters — a body with any
  non-ASCII text has a byte count larger than its length in TCHARs.
- **Response bodies are decoded from UTF-8** into the client's TCHAR text.
  Malformed byte sequences become U+FFFD rather than failing the request.
- **`Content-Length` is honoured.** The WinSock transport keeps reading until the
  declared length has arrived; a body that stops short is reported as a failed
  request, not handed to the caller as a short JSON document.
- **`Transfer-Encoding: chunked` is honoured.** The WinSock transport de-frames
  the chunks itself and requires the terminating zero-length chunk before it
  considers the response complete. On the WinInet transport the OS stack does the
  de-framing.
- A response carrying **neither** framing header is read until the peer closes,
  which is why the client sends `Connection: close` unless a caller has set its
  own `Connection` header.
- Responses are capped at 512 KB (`kMaxResponseBytes`) on both transports;
  a larger reply fails the request rather than growing the buffer indefinitely on
  a 64 MB device.

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
│ Hold the token in HbClient, and  │
│ write it back to hb_conf.json so │
│ a restart can resume the session │
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
Content-Type: application/json; charset=utf-8
Accept: application/json
```

**Request Body** (both values are JSON-escaped, and the body is built in a
growable buffer — an API key or device id has no length limit):
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
1. Obtained at startup by exchanging `deviceId` + `apiKey`. Failing is **not**
   fatal — a device that boots out of coverage still scans and queues
2. Held by `HbClient` and sent as `Authorization: Bearer …` on every later call
3. Only a non-empty **string** `token` member is accepted. A null, a number or
   `""` leaves the client unauthenticated rather than carrying a garbage bearer
   value into every subsequent request
4. Cached in `hb_conf.json` so a restart — a battery swap ends the process
   several times a shift — does not need a fresh handshake
5. A `401` drops the session; the caller re-authenticates once and retries that
   one request. Lookups that fail for any other reason are **not** retried: a
   404 for an unknown barcode should not cost the operator a second round trip

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

> ⚠️ **Nothing in the running application calls this endpoint.**
> `SyncEngine::Sync` replays the queue one entry at a time through the ordinary
> item endpoints (`GET /api/v1/items/{barcode}`,
> `PUT /api/v1/items/{barcode}/location`, `PUT /api/v1/items/{id}`) and
> acknowledges each one in the journal separately.
>
> `HbClient::SyncPendingTransactions` does exist and does POST here, but it is
> currently reached only from the integration tests — and the body it builds is
> **not** the shape below: it sends each transaction as an escaped **string**
> (the raw journal record line), i.e.
> `{"deviceId":"…","transactions":["[…] TRANS 00000042: …", …]}`.
> Treat the request/response shapes in this section as the intended server
> contract for a future batching client, not as something the device sends today.

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

**What the client sends** (`HbClient::SetAuthHeaders` plus the transport):
```http
Content-Type: application/json; charset=utf-8
Accept: application/json
Authorization: Bearer {token}        (only once a session token is held)
Host: {host}[:{port}]                (WinSock builds it; WinInet supplies its own)
Content-Length: {wire bytes}         (WinSock writes it; WinInet derives it from
                                      the same UTF-8 byte count)
Connection: close                    (WinSock only, unless a caller overrides it)
```

The charset is stated explicitly because bodies really are UTF-8 encoded — see
[Encoding and Framing](#-encoding-and-framing-what-the-client-actually-does).

**Not sent today** (a server must not depend on them):
```http
User-Agent, X-Device-ID, X-Request-ID, X-Client-Version
```
The device identifies itself in the auth request body (`deviceId`), not in a
header. `HttpClient::AddHeader` is public, so any of these can be added by a
caller if the server starts requiring them.

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

**Queue Entry Format** (in the Journal — one CRLF-terminated, UTF-8 line each):
```
[2026-07-25 09:14:09] TRANS 00000042: [51234] ITEM_SCAN: SCAN:1234567890
[2026-07-25 09:14:31] TRANS 00000043: [73180] ITEM_SCAN: SCANLOC:10:9876543210loc_b02
[2026-07-25 09:15:02] TRANS 00000044: [98220] ITEM_UPDATE: UPDATE:{"id":"123","quantity":50}
[2026-07-25 09:16:44] SYNCED 00000042
```

`TRANS <seq>` is the record header the journal adds; the rest is the payload the
sync engine wrote. A `SYNCED` marker acknowledges a **sequence number**, so a
payload that happens to contain the word "SYNCED" cannot be mistaken for one.
The location form is length-prefixed (`SCANLOC:<barcodeLength>:…`) because a
barcode may legally contain any separator character. The record format is
documented in `include/Journal.hpp` and in [DESIGN.md](DESIGN.md).

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
// SyncEngine::CheckConnectivity - the host is parsed out of apiBaseUrl and
// encoded to UTF-8 rather than each character being truncated to a byte.
WSADATA wsaData;
bool winsockStarted = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);

struct hostent* hostInfo = gethostbyname(asciiHost);

// Released only if it was actually acquired: an unconditional WSACleanup after
// a failed WSAStartup drops somebody else's reference and can tear WinSock down
// while a request is in flight.
if (winsockStarted) {
    WSACleanup();
}
return (hostInfo != NULL);
```

**Caching**: the answer is cached for **5 seconds**
(`kConnectivityCacheMs` in `src/SyncEngine.cpp`). The probe is a blocking DNS
lookup that costs seconds on a GPRS link, and the UI status line, `IsOnline()`
and `Sync()` all ask for it. A sync in which every transaction failed drops the
cached answer immediately rather than claiming "online" for the rest of the
window.

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
