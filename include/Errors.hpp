#ifndef ERRORS_HPP
#define ERRORS_HPP

/**
 * Error codes and status enumerations for HBXClient
 * Provides standardized error handling across all components
 */

namespace HBX {

// ============================================================================
// RESULT CODES
// ============================================================================

/**
 * General result codes for all operations
 */
enum ResultCode {
    RESULT_SUCCESS = 0,
    RESULT_ERROR = -1,
    RESULT_INVALID_PARAM = -2,
    RESULT_OUT_OF_MEMORY = -3,
    RESULT_NOT_INITIALIZED = -4,
    RESULT_ALREADY_INITIALIZED = -5,
    RESULT_TIMEOUT = -6,
    RESULT_CANCELLED = -7,
    RESULT_NOT_SUPPORTED = -8,
    RESULT_PERMISSION_DENIED = -9,
    RESULT_FILE_NOT_FOUND = -10,
    RESULT_FILE_IO_ERROR = -11
};

// ============================================================================
// HTTP ERROR CODES
// ============================================================================

/**
 * HTTP client specific error codes
 */
enum HttpError {
    HTTP_ERROR_NONE = 0,
    HTTP_ERROR_INVALID_URL = 1001,
    HTTP_ERROR_DNS_FAILED = 1002,
    HTTP_ERROR_CONNECTION_FAILED = 1003,
    HTTP_ERROR_TIMEOUT = 1004,
    HTTP_ERROR_SEND_FAILED = 1005,
    HTTP_ERROR_RECEIVE_FAILED = 1006,
    HTTP_ERROR_INVALID_RESPONSE = 1007,
    HTTP_ERROR_SSL_FAILED = 1008,
    HTTP_ERROR_PROTOCOL_ERROR = 1009
};

/**
 * Convert HTTP status code to string
 * @param statusCode HTTP status code
 * @return String description
 */
inline const TCHAR* HttpStatusToString(int statusCode)
{
    switch (statusCode) {
        case 200: return TEXT("OK");
        case 201: return TEXT("Created");
        case 204: return TEXT("No Content");
        case 400: return TEXT("Bad Request");
        case 401: return TEXT("Unauthorized");
        case 403: return TEXT("Forbidden");
        case 404: return TEXT("Not Found");
        case 409: return TEXT("Conflict");
        case 422: return TEXT("Unprocessable Entity");
        case 429: return TEXT("Too Many Requests");
        case 500: return TEXT("Internal Server Error");
        case 502: return TEXT("Bad Gateway");
        case 503: return TEXT("Service Unavailable");
        default: return TEXT("Unknown");
    }
}

/**
 * Check if HTTP status code indicates success
 * @param statusCode HTTP status code
 * @return true if successful (200-299)
 */
inline bool IsHttpSuccess(int statusCode)
{
    return (statusCode >= 200 && statusCode < 300);
}

/**
 * Check if HTTP status code indicates client error
 * @param statusCode HTTP status code
 * @return true if client error (400-499)
 */
inline bool IsHttpClientError(int statusCode)
{
    return (statusCode >= 400 && statusCode < 500);
}

/**
 * Check if HTTP status code indicates server error
 * @param statusCode HTTP status code
 * @return true if server error (500-599)
 */
inline bool IsHttpServerError(int statusCode)
{
    return (statusCode >= 500 && statusCode < 600);
}

// ============================================================================
// API ERROR CODES
// ============================================================================

/**
 * API client specific error codes
 */
enum ApiError {
    API_ERROR_NONE = 0,
    API_ERROR_NOT_AUTHENTICATED = 2001,
    API_ERROR_TOKEN_EXPIRED = 2002,
    API_ERROR_INVALID_REQUEST = 2003,
    API_ERROR_INVALID_RESPONSE = 2004,
    API_ERROR_NETWORK_ERROR = 2005,
    API_ERROR_SERVER_ERROR = 2006,
    API_ERROR_RATE_LIMITED = 2007,
    API_ERROR_ITEM_NOT_FOUND = 2008,
    API_ERROR_LOCATION_NOT_FOUND = 2009,
    API_ERROR_VALIDATION_FAILED = 2010
};

// ============================================================================
// SCANNER ERROR CODES
// ============================================================================

/**
 * Scanner hardware specific error codes
 */
enum ScannerError {
    SCANNER_ERROR_NONE = 0,
    SCANNER_ERROR_NOT_INITIALIZED = 3001,
    SCANNER_ERROR_OPEN_FAILED = 3002,
    SCANNER_ERROR_ALREADY_OPEN = 3003,
    SCANNER_ERROR_NOT_ENABLED = 3004,
    SCANNER_ERROR_READ_FAILED = 3005,
    SCANNER_ERROR_TIMEOUT = 3006,
    SCANNER_ERROR_HARDWARE_FAULT = 3007,
    SCANNER_ERROR_DECODE_FAILED = 3008,
    SCANNER_ERROR_UNSUPPORTED_SYMBOLOGY = 3009
};

// ============================================================================
// SYNC ERROR CODES
// ============================================================================

/**
 * Synchronization engine specific error codes
 */
enum SyncError {
    SYNC_ERROR_NONE = 0,
    SYNC_ERROR_OFFLINE = 4001,
    SYNC_ERROR_QUEUE_FULL = 4002,
    SYNC_ERROR_INVALID_TRANSACTION = 4003,
    SYNC_ERROR_JOURNAL_ERROR = 4004,
    SYNC_ERROR_PARTIAL_FAILURE = 4005,
    SYNC_ERROR_ALL_FAILED = 4006,
    SYNC_ERROR_CONNECTIVITY_LOST = 4007
};

// ============================================================================
// JSON ERROR CODES
// ============================================================================

/**
 * JSON parsing specific error codes
 */
enum JsonError {
    JSON_ERROR_NONE = 0,
    JSON_ERROR_INVALID_FORMAT = 5001,
    JSON_ERROR_UNEXPECTED_TOKEN = 5002,
    JSON_ERROR_MISSING_FIELD = 5003,
    JSON_ERROR_INVALID_VALUE = 5004,
    JSON_ERROR_BUFFER_OVERFLOW = 5005,
    JSON_ERROR_NESTING_TOO_DEEP = 5006
};

// ============================================================================
// CONFIG ERROR CODES
// ============================================================================

/**
 * Configuration management specific error codes
 */
enum ConfigError {
    CONFIG_ERROR_NONE = 0,
    CONFIG_ERROR_FILE_NOT_FOUND = 6001,
    CONFIG_ERROR_READ_FAILED = 6002,
    CONFIG_ERROR_WRITE_FAILED = 6003,
    CONFIG_ERROR_PARSE_FAILED = 6004,
    CONFIG_ERROR_INVALID_VALUE = 6005,
    CONFIG_ERROR_MISSING_REQUIRED = 6006
};

// ============================================================================
// ERROR SEVERITY
// ============================================================================

/**
 * Error severity levels for logging and handling
 */
enum ErrorSeverity {
    SEVERITY_INFO = 0,        // Informational, no action needed
    SEVERITY_WARNING = 1,     // Warning, operation continues
    SEVERITY_ERROR = 2,       // Error, operation failed but recoverable
    SEVERITY_CRITICAL = 3,    // Critical, application state compromised
    SEVERITY_FATAL = 4        // Fatal, application must terminate
};

// ============================================================================
// ERROR CONTEXT
// ============================================================================

/**
 * Error context structure for detailed error reporting
 */
struct ErrorContext {
    int errorCode;            // Primary error code
    int extendedCode;         // Extended/sub error code
    ErrorSeverity severity;   // Error severity
    TCHAR* message;           // Human-readable error message
    TCHAR* details;           // Additional error details
    const TCHAR* component;   // Component that generated the error

    ErrorContext()
        : errorCode(RESULT_SUCCESS)
        , extendedCode(0)
        , severity(SEVERITY_INFO)
        , message(NULL)
        , details(NULL)
        , component(NULL)
    {
    }

    ~ErrorContext()
    {
        if (message) {
            delete[] message;
            message = NULL;
        }
        if (details) {
            delete[] details;
            details = NULL;
        }
    }

    /**
     * Set error message
     * @param msg Error message to set
     */
    void SetMessage(const TCHAR* msg)
    {
        if (message) {
            delete[] message;
        }
        if (msg) {
            int len = lstrlen(msg) + 1;
            message = new TCHAR[len];
            lstrcpy(message, msg);
        } else {
            message = NULL;
        }
    }

    /**
     * Set error details
     * @param det Error details to set
     */
    void SetDetails(const TCHAR* det)
    {
        if (details) {
            delete[] details;
        }
        if (det) {
            int len = lstrlen(det) + 1;
            details = new TCHAR[len];
            lstrcpy(details, det);
        } else {
            details = NULL;
        }
    }

    /**
     * Check if error context indicates success
     * @return true if no error
     */
    bool IsSuccess() const
    {
        return (errorCode == RESULT_SUCCESS);
    }

    /**
     * Check if error is recoverable
     * @return true if severity is below CRITICAL
     */
    bool IsRecoverable() const
    {
        return (severity < SEVERITY_CRITICAL);
    }
};

} // namespace HBX

#endif // ERRORS_HPP
