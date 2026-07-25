#ifndef HBX_STRUTIL_HPP
#define HBX_STRUTIL_HPP

#include <windows.h>

namespace HBX {

/**
 * Bounded string, JSON-escaping and UTF-8 helpers.
 *
 * Every routine here is written for Visual Studio 2008 / C++03 and compiles
 * unchanged for both the ARMV4I device build (TCHAR == WCHAR) and the host
 * test harness (TCHAR == char). Nothing in this file depends on a Win32
 * codepage API, so there is no #ifdef'd code path that can silently rot: the
 * TCHAR width is inspected with sizeof() and the compiler folds the branch.
 *
 * Rationale: the original code formatted unbounded, caller-supplied strings
 * into fixed stack buffers with wsprintf. On Windows CE wsprintf caps a single
 * call at 1024 characters, but that cap does not bound a call that starts at an
 * offset into a buffer, and it does not bound the host build at all. Every such
 * site now goes through Str::Copy / Str::Append / Str::Buffer, which truncate
 * safely and always NUL-terminate.
 */
namespace Str {

// ---------------------------------------------------------------------------
// Bounded copy / append
//
// `cap` is the capacity of `dst` in TCHARs, including the NUL. All of these
// always NUL-terminate when cap > 0, and return true only if the whole source
// fitted (false means the result was truncated).
// ---------------------------------------------------------------------------

bool Copy(TCHAR* dst, int cap, const TCHAR* src);
bool CopyN(TCHAR* dst, int cap, const TCHAR* src, int srcLen);
bool Append(TCHAR* dst, int cap, const TCHAR* src);
bool AppendChar(TCHAR* dst, int cap, TCHAR ch);
bool AppendInt(TCHAR* dst, int cap, long value);

/** Heap copy of `src`; caller owns the result and must delete[] it. */
TCHAR* Dup(const TCHAR* src);

/** Heap copy of the first `len` TCHARs of `src`, NUL-terminated. */
TCHAR* DupN(const TCHAR* src, int len);

/** lstrlen that tolerates NULL. */
int Length(const TCHAR* s);

/**
 * Strict decimal parse. Accepts optional leading '-'/'+' and digits only;
 * rejects an empty or non-numeric string. Values outside the int range are
 * clamped to INT_MIN / INT_MAX rather than wrapping. Returns false if the
 * string was not a well-formed integer.
 */
bool ParseInt(const TCHAR* s, int* out);

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

/**
 * Escapes `src` per RFC 8259 into `dst` without adding the surrounding quotes:
 * '"' and '\\' are backslash-escaped, and the control characters below 0x20
 * become \b \f \n \r \t or \u00XX. Always NUL-terminates; returns false if the
 * escaped form did not fit (in which case `dst` holds a valid, shorter escape
 * sequence -- never a half-written one).
 */
bool JsonEscape(TCHAR* dst, int cap, const TCHAR* src);

// ---------------------------------------------------------------------------
// UTF-8
//
// The journal, the WinSock transport and the WinInet transport all need to move
// between TCHAR text and bytes on the wire. The original code did that with
// `(char)tchar` truncation loops, which silently mangled anything outside
// US-ASCII and left uninitialised tails in the destination buffer.
// ---------------------------------------------------------------------------

/**
 * Encodes `src` as UTF-8 into `dst` (`cap` bytes including the NUL).
 * On the device a TCHAR is UTF-16, so surrogate pairs are combined into the
 * proper 4-byte sequences. On the host TCHAR is a byte and values >= 0x80 are
 * passed through unchanged, since they are already encoded.
 * Always NUL-terminates; returns false on truncation. Truncation happens on a
 * character boundary, so the result is never a partial multi-byte sequence.
 */
bool ToUtf8(char* dst, int cap, const TCHAR* src);

/** Bytes required to hold ToUtf8(src), including the terminating NUL. */
int Utf8Size(const TCHAR* src);

/**
 * Decodes UTF-8 `src` into `dst` (`cap` TCHARs including the NUL). Malformed
 * bytes are replaced with U+FFFD rather than being rejected, so a corrupt
 * journal line still round-trips to something displayable. Always
 * NUL-terminates; returns false on truncation.
 */
bool FromUtf8(TCHAR* dst, int cap, const char* src);

/** Heap UTF-8 encoding of `src`; caller must delete[] the result. */
char* ToUtf8Alloc(const TCHAR* src);

/** Heap TCHAR decoding of UTF-8 `src`; caller must delete[] the result. */
TCHAR* FromUtf8Alloc(const char* src);

// ---------------------------------------------------------------------------
// Growable text buffer
// ---------------------------------------------------------------------------

/**
 * A minimal growable TCHAR buffer used to build JSON documents and HTTP
 * requests whose final size is not known up front. Replaces the fixed
 * `new TCHAR[2048]` / `char request[4096]` buffers that the callers used to
 * format unbounded input into.
 *
 * Allocation failure is sticky: once the buffer fails to grow, every further
 * append is a no-op and Failed() stays true, so callers can check once at the
 * end instead of after every append.
 */
class Buffer {
public:
    Buffer();
    ~Buffer();

    bool Append(const TCHAR* s);
    bool AppendN(const TCHAR* s, int len);
    bool AppendChar(TCHAR ch);
    bool AppendInt(long value);

    /** Appends `"<json-escaped s>"`, including the surrounding quotes. */
    bool AppendJsonString(const TCHAR* s);

    /** Appends `"<key>":"<json-escaped value>"`. */
    bool AppendJsonPair(const TCHAR* key, const TCHAR* value);

    /** Appends `"<key>":<value>` for a numeric value. */
    bool AppendJsonInt(const TCHAR* key, long value);

    /** Always NUL-terminated, never NULL. */
    const TCHAR* Get() const;
    int Length() const;
    bool Failed() const;

    void Clear();

    /**
     * Hands the buffer's storage to the caller, who must delete[] it. The
     * Buffer is left empty. Returns NULL if the buffer ever failed to grow.
     */
    TCHAR* Detach();

private:
    bool Reserve(int extra);

    TCHAR* m_data;
    int m_len;
    int m_cap;
    bool m_failed;

    // Not copyable: the class owns a raw allocation.
    Buffer(const Buffer&);
    Buffer& operator=(const Buffer&);
};

} // namespace Str
} // namespace HBX

#endif // HBX_STRUTIL_HPP
