#ifndef COMMON_HPP
#define COMMON_HPP

#include <windows.h>

/**
 * Common utilities and constants for HBXClient
 * Provides shared functionality across all components
 */

namespace HBX {
namespace Common {

// ============================================================================
// CONSTANTS
// ============================================================================

// Buffer size constants
const int MAX_URL_LENGTH = 1024;
const int MAX_HOST_LENGTH = 256;
const int MAX_PATH_LENGTH = 1024;
const int MAX_HEADER_LENGTH = 512;
const int MAX_BARCODE_LENGTH = 128;
const int MAX_NAME_LENGTH = 256;
const int MAX_DESCRIPTION_LENGTH = 1024;
const int MAX_ERROR_MESSAGE_LENGTH = 512;
const int MAX_JSON_BUFFER_SIZE = 8192;
const int MAX_HTTP_REQUEST_SIZE = 4096;
const int MAX_HTTP_RESPONSE_SIZE = 8192;

// Network constants
const DWORD DEFAULT_HTTP_TIMEOUT_MS = 30000; // 30 seconds
const int DEFAULT_HTTP_PORT = 80;
const int DEFAULT_HTTPS_PORT = 443;

// Sync constants
const int DEFAULT_SYNC_INTERVAL_SECONDS = 300; // 5 minutes
const int MAX_QUEUE_TRANSACTIONS = 1000;

// Scanner constants
const int SCANNER_THREAD_SLEEP_MS = 100;
const int SCANNER_SHUTDOWN_TIMEOUT_MS = 5000;

// ============================================================================
// STRING UTILITIES
// ============================================================================

/**
 * Convert TCHAR string to ASCII char string
 * @param tcharStr Input TCHAR string
 * @param charStr Output char string buffer
 * @param maxLen Maximum length of output buffer
 * @return Number of characters converted (excluding null terminator)
 */
inline int TCharToChar(const TCHAR* tcharStr, char* charStr, int maxLen)
{
    if (!tcharStr || !charStr || maxLen <= 0) {
        return 0;
    }

    int i;
    for (i = 0; i < maxLen - 1 && tcharStr[i] != '\0'; i++) {
        charStr[i] = (char)tcharStr[i];
    }
    charStr[i] = '\0';

    return i;
}

/**
 * Convert ASCII char string to TCHAR string
 * @param charStr Input char string
 * @param tcharStr Output TCHAR string buffer
 * @param maxLen Maximum length of output buffer
 * @return Number of characters converted (excluding null terminator)
 */
inline int CharToTChar(const char* charStr, TCHAR* tcharStr, int maxLen)
{
    if (!charStr || !tcharStr || maxLen <= 0) {
        return 0;
    }

    int i;
    for (i = 0; i < maxLen - 1 && charStr[i] != '\0'; i++) {
        tcharStr[i] = (TCHAR)charStr[i];
    }
    tcharStr[i] = '\0';

    return i;
}

/**
 * Safe string copy with length limit
 * @param dest Destination buffer
 * @param src Source string
 * @param maxLen Maximum length to copy (including null terminator)
 * @return Pointer to destination
 */
inline TCHAR* SafeStrCopy(TCHAR* dest, const TCHAR* src, int maxLen)
{
    if (!dest || !src || maxLen <= 0) {
        return dest;
    }

    int i;
    for (i = 0; i < maxLen - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';

    return dest;
}

/**
 * Allocate and copy a string
 * @param src Source string to duplicate
 * @return Newly allocated copy (caller must delete[])
 */
inline TCHAR* StrDuplicate(const TCHAR* src)
{
    if (!src) {
        return NULL;
    }

    int len = lstrlen(src) + 1;
    TCHAR* copy = new TCHAR[len];
    if (copy) {
        lstrcpy(copy, src);
    }

    return copy;
}

/**
 * Free and null a string pointer
 * @param str Pointer to string pointer (will be set to NULL)
 */
inline void StrFree(TCHAR** str)
{
    if (str && *str) {
        delete[] *str;
        *str = NULL;
    }
}

// ============================================================================
// VALIDATION UTILITIES
// ============================================================================

/**
 * Check if a string is null or empty
 * @param str String to check
 * @return true if null or empty
 */
inline bool IsNullOrEmpty(const TCHAR* str)
{
    return (!str || lstrlen(str) == 0);
}

/**
 * Check if a string is valid (not null and not empty)
 * @param str String to check
 * @return true if valid
 */
inline bool IsValidString(const TCHAR* str)
{
    return !IsNullOrEmpty(str);
}

/**
 * Validate barcode format (basic check)
 * @param barcode Barcode string to validate
 * @return true if valid barcode format
 */
inline bool IsValidBarcode(const TCHAR* barcode)
{
    if (IsNullOrEmpty(barcode)) {
        return false;
    }

    int len = lstrlen(barcode);
    // Typical barcodes are 8-14 digits for UPC/EAN, up to 128 for Code128
    if (len < 3 || len > MAX_BARCODE_LENGTH) {
        return false;
    }

    return true;
}

/**
 * Validate URL format (basic check)
 * @param url URL string to validate
 * @return true if valid URL format
 */
inline bool IsValidUrl(const TCHAR* url)
{
    if (IsNullOrEmpty(url)) {
        return false;
    }

    // Check for protocol
    if (wcsncmp(url, TEXT("http://"), 7) != 0 &&
        wcsncmp(url, TEXT("https://"), 8) != 0) {
        return false;
    }

    return true;
}

// ============================================================================
// MATH UTILITIES
// ============================================================================

/**
 * Clamp a value between min and max
 * @param value Value to clamp
 * @param min Minimum value
 * @param max Maximum value
 * @return Clamped value
 */
template<typename T>
inline T Clamp(T value, T min, T max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * Get minimum of two values
 * @param a First value
 * @param b Second value
 * @return Minimum value
 */
template<typename T>
inline T Min(T a, T b)
{
    return (a < b) ? a : b;
}

/**
 * Get maximum of two values
 * @param a First value
 * @param b Second value
 * @return Maximum value
 */
template<typename T>
inline T Max(T a, T b)
{
    return (a > b) ? a : b;
}

} // namespace Common
} // namespace HBX

#endif // COMMON_HPP
