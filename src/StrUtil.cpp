#include "../include/StrUtil.hpp"

#include <string.h>
#include <limits.h>

namespace HBX {
namespace Str {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Reads a TCHAR as an unsigned code unit. TCHAR is `char` on the host shim and
// may be signed there, so the cast through unsigned char matters.
static unsigned long CodeUnit(TCHAR c)
{
    if (sizeof(TCHAR) == 1) {
        return (unsigned long)(unsigned char)c;
    }
    return (unsigned long)(unsigned short)c;
}

static const unsigned long kReplacementChar = 0xFFFDUL;

// ---------------------------------------------------------------------------
// Bounded copy / append
// ---------------------------------------------------------------------------

int Length(const TCHAR* s)
{
    if (!s) {
        return 0;
    }
    return lstrlen(s);
}

bool Copy(TCHAR* dst, int cap, const TCHAR* src)
{
    if (!dst || cap <= 0) {
        return false;
    }
    if (!src) {
        dst[0] = 0;
        return true;
    }

    int i = 0;
    while (src[i] != 0 && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;

    return (src[i] == 0);
}

bool CopyN(TCHAR* dst, int cap, const TCHAR* src, int srcLen)
{
    if (!dst || cap <= 0) {
        return false;
    }
    if (!src || srcLen <= 0) {
        dst[0] = 0;
        return true;
    }

    int i = 0;
    while (i < srcLen && src[i] != 0 && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;

    // True only if we stopped because we ran out of source, not capacity.
    return (i == srcLen || src[i] == 0);
}

bool Append(TCHAR* dst, int cap, const TCHAR* src)
{
    if (!dst || cap <= 0) {
        return false;
    }
    int len = lstrlen(dst);
    if (len >= cap - 1) {
        dst[cap - 1] = 0;
        return (!src || src[0] == 0);
    }
    return Copy(dst + len, cap - len, src);
}

bool AppendChar(TCHAR* dst, int cap, TCHAR ch)
{
    if (!dst || cap <= 0) {
        return false;
    }
    int len = lstrlen(dst);
    if (len >= cap - 1) {
        return false;
    }
    dst[len] = ch;
    dst[len + 1] = 0;
    return true;
}

bool AppendInt(TCHAR* dst, int cap, long value)
{
    // 20 digits covers a 64-bit value; plus sign and NUL.
    TCHAR digits[24];
    int n = 0;
    bool negative = false;
    unsigned long magnitude;

    if (value < 0) {
        negative = true;
        // Negating LONG_MIN overflows, so build the magnitude unsigned.
        magnitude = (unsigned long)(-(value + 1)) + 1UL;
    } else {
        magnitude = (unsigned long)value;
    }

    if (magnitude == 0) {
        digits[n++] = (TCHAR)'0';
    }
    while (magnitude > 0) {
        digits[n++] = (TCHAR)('0' + (int)(magnitude % 10UL));
        magnitude /= 10UL;
    }

    TCHAR text[26];
    int pos = 0;
    if (negative) {
        text[pos++] = (TCHAR)'-';
    }
    while (n > 0) {
        text[pos++] = digits[--n];
    }
    text[pos] = 0;

    return Append(dst, cap, text);
}

TCHAR* Dup(const TCHAR* src)
{
    return DupN(src, Length(src));
}

TCHAR* DupN(const TCHAR* src, int len)
{
    if (len < 0) {
        len = 0;
    }

    TCHAR* copy = new TCHAR[len + 1];
    if (!copy) {
        return NULL;
    }

    for (int i = 0; i < len; i++) {
        copy[i] = src[i];
    }
    copy[len] = 0;

    return copy;
}

bool ParseInt(const TCHAR* s, int* out)
{
    if (!s || !out) {
        return false;
    }

    int i = 0;
    while (s[i] == (TCHAR)' ' || s[i] == (TCHAR)'\t') {
        i++;
    }

    bool negative = false;
    if (s[i] == (TCHAR)'-') {
        negative = true;
        i++;
    } else if (s[i] == (TCHAR)'+') {
        i++;
    }

    if (s[i] < (TCHAR)'0' || s[i] > (TCHAR)'9') {
        return false; // no digits at all
    }

    // Accumulate with a saturating guard so a long digit run clamps instead of
    // wrapping to garbage (ItemView's quantity field used to wrap).
    unsigned long limit = negative ? 2147483648UL : 2147483647UL;
    unsigned long value = 0;
    bool saturated = false;

    while (s[i] >= (TCHAR)'0' && s[i] <= (TCHAR)'9') {
        unsigned long digit = (unsigned long)(s[i] - (TCHAR)'0');
        if (!saturated) {
            if (value > (limit - digit) / 10UL) {
                saturated = true;
                value = limit;
            } else {
                value = value * 10UL + digit;
            }
        }
        i++;
    }

    while (s[i] == (TCHAR)' ' || s[i] == (TCHAR)'\t') {
        i++;
    }
    if (s[i] != 0) {
        return false; // trailing junk
    }

    if (negative) {
        *out = (value >= 2147483648UL) ? INT_MIN : -(int)value;
    } else {
        *out = (int)value;
    }

    return !saturated;
}

// ---------------------------------------------------------------------------
// JSON escaping
// ---------------------------------------------------------------------------

static bool AppendEscape(TCHAR* dst, int cap, int* pos, const TCHAR* seq, int seqLen)
{
    if (*pos + seqLen > cap - 1) {
        return false;
    }
    for (int i = 0; i < seqLen; i++) {
        dst[(*pos)++] = seq[i];
    }
    return true;
}

bool JsonEscape(TCHAR* dst, int cap, const TCHAR* src)
{
    if (!dst || cap <= 0) {
        return false;
    }

    dst[0] = 0;
    if (!src) {
        return true;
    }

    int pos = 0;
    for (int i = 0; src[i] != 0; i++) {
        unsigned long c = CodeUnit(src[i]);
        bool ok;

        switch (c) {
        case '"': {
            TCHAR seq[2]; seq[0] = (TCHAR)'\\'; seq[1] = (TCHAR)'"';
            ok = AppendEscape(dst, cap, &pos, seq, 2);
            break;
        }
        case '\\': {
            TCHAR seq[2]; seq[0] = (TCHAR)'\\'; seq[1] = (TCHAR)'\\';
            ok = AppendEscape(dst, cap, &pos, seq, 2);
            break;
        }
        case '\b': case '\f': case '\n': case '\r': case '\t': {
            TCHAR seq[2];
            seq[0] = (TCHAR)'\\';
            seq[1] = (TCHAR)(c == '\b' ? 'b' : c == '\f' ? 'f' :
                             c == '\n' ? 'n' : c == '\r' ? 'r' : 't');
            ok = AppendEscape(dst, cap, &pos, seq, 2);
            break;
        }
        default:
            if (c < 0x20) {
                // \u00XX for the remaining control characters.
                static const char kHex[] = "0123456789abcdef";
                TCHAR seq[6];
                seq[0] = (TCHAR)'\\';
                seq[1] = (TCHAR)'u';
                seq[2] = (TCHAR)'0';
                seq[3] = (TCHAR)'0';
                seq[4] = (TCHAR)kHex[(c >> 4) & 0xF];
                seq[5] = (TCHAR)kHex[c & 0xF];
                ok = AppendEscape(dst, cap, &pos, seq, 6);
            } else {
                if (pos > cap - 2) {
                    ok = false;
                } else {
                    dst[pos++] = src[i];
                    ok = true;
                }
            }
            break;
        }

        if (!ok) {
            // Stop on a whole-escape boundary so the result stays valid JSON.
            dst[pos] = 0;
            return false;
        }
    }

    dst[pos] = 0;
    return true;
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

// Reads the next Unicode scalar from a TCHAR string, advancing *i.
// On the device this combines UTF-16 surrogate pairs. On the host each byte is
// its own value (already-encoded text is passed through unchanged).
static unsigned long NextScalar(const TCHAR* src, int* i)
{
    unsigned long c = CodeUnit(src[*i]);
    (*i)++;

    if (sizeof(TCHAR) > 1 && c >= 0xD800UL && c <= 0xDBFFUL) {
        unsigned long low = CodeUnit(src[*i]);
        if (low >= 0xDC00UL && low <= 0xDFFFUL) {
            (*i)++;
            return 0x10000UL + ((c - 0xD800UL) << 10) + (low - 0xDC00UL);
        }
        return kReplacementChar; // unpaired high surrogate
    }
    if (sizeof(TCHAR) > 1 && c >= 0xDC00UL && c <= 0xDFFFUL) {
        return kReplacementChar; // unpaired low surrogate
    }

    return c;
}

// Number of UTF-8 bytes for a scalar. On the host, bytes >= 0x80 are treated as
// already-encoded and cost exactly one byte.
static int EncodedSize(unsigned long c)
{
    if (sizeof(TCHAR) == 1) {
        return 1;
    }
    if (c < 0x80UL) {
        return 1;
    }
    if (c < 0x800UL) {
        return 2;
    }
    if (c < 0x10000UL) {
        return 3;
    }
    return 4;
}

static void Encode(char* dst, unsigned long c, int size)
{
    switch (size) {
    case 1:
        dst[0] = (char)(unsigned char)c;
        break;
    case 2:
        dst[0] = (char)(unsigned char)(0xC0UL | (c >> 6));
        dst[1] = (char)(unsigned char)(0x80UL | (c & 0x3FUL));
        break;
    case 3:
        dst[0] = (char)(unsigned char)(0xE0UL | (c >> 12));
        dst[1] = (char)(unsigned char)(0x80UL | ((c >> 6) & 0x3FUL));
        dst[2] = (char)(unsigned char)(0x80UL | (c & 0x3FUL));
        break;
    default:
        dst[0] = (char)(unsigned char)(0xF0UL | (c >> 18));
        dst[1] = (char)(unsigned char)(0x80UL | ((c >> 12) & 0x3FUL));
        dst[2] = (char)(unsigned char)(0x80UL | ((c >> 6) & 0x3FUL));
        dst[3] = (char)(unsigned char)(0x80UL | (c & 0x3FUL));
        break;
    }
}

int Utf8Size(const TCHAR* src)
{
    if (!src) {
        return 1;
    }

    int bytes = 1; // terminating NUL
    int i = 0;
    while (src[i] != 0) {
        bytes += EncodedSize(NextScalar(src, &i));
    }
    return bytes;
}

bool ToUtf8(char* dst, int cap, const TCHAR* src)
{
    if (!dst || cap <= 0) {
        return false;
    }

    dst[0] = 0;
    if (!src) {
        return true;
    }

    int pos = 0;
    int i = 0;
    while (src[i] != 0) {
        int before = i;
        unsigned long c = NextScalar(src, &i);
        int size = EncodedSize(c);

        if (pos + size > cap - 1) {
            // Truncate on a character boundary, never mid-sequence.
            (void)before;
            dst[pos] = 0;
            return false;
        }

        Encode(dst + pos, c, size);
        pos += size;
    }

    dst[pos] = 0;
    return true;
}

bool FromUtf8(TCHAR* dst, int cap, const char* src)
{
    if (!dst || cap <= 0) {
        return false;
    }

    dst[0] = 0;
    if (!src) {
        return true;
    }

    int pos = 0;
    int i = 0;

    while (src[i] != '\0') {
        unsigned long c;
        unsigned char b0 = (unsigned char)src[i];
        int extra;

        if (sizeof(TCHAR) == 1) {
            // Host build: TCHAR is a byte, so the encoded form is the value.
            c = b0;
            extra = 0;
            i++;
        } else {
            if (b0 < 0x80) {
                c = b0; extra = 0;
            } else if ((b0 & 0xE0) == 0xC0) {
                c = b0 & 0x1FUL; extra = 1;
            } else if ((b0 & 0xF0) == 0xE0) {
                c = b0 & 0x0FUL; extra = 2;
            } else if ((b0 & 0xF8) == 0xF0) {
                c = b0 & 0x07UL; extra = 3;
            } else {
                c = kReplacementChar; extra = -1; // stray continuation / invalid
            }

            i++;
            if (extra < 0) {
                extra = 0;
            } else {
                for (int k = 0; k < extra; k++) {
                    unsigned char cb = (unsigned char)src[i];
                    if ((cb & 0xC0) != 0x80) {
                        c = kReplacementChar;
                        break;
                    }
                    c = (c << 6) | (unsigned long)(cb & 0x3F);
                    i++;
                }
            }
        }

        // Emit as one or two TCHARs (surrogate pair above the BMP).
        if (sizeof(TCHAR) > 1 && c > 0xFFFFUL) {
            if (c > 0x10FFFFUL) {
                c = kReplacementChar;
            }
        }

        if (sizeof(TCHAR) > 1 && c > 0xFFFFUL) {
            if (pos + 2 > cap - 1) {
                dst[pos] = 0;
                return false;
            }
            unsigned long v = c - 0x10000UL;
            dst[pos++] = (TCHAR)(unsigned short)(0xD800UL + (v >> 10));
            dst[pos++] = (TCHAR)(unsigned short)(0xDC00UL + (v & 0x3FFUL));
        } else {
            if (pos + 1 > cap - 1) {
                dst[pos] = 0;
                return false;
            }
            dst[pos++] = (TCHAR)c;
        }
    }

    dst[pos] = 0;
    return true;
}

char* ToUtf8Alloc(const TCHAR* src)
{
    int size = Utf8Size(src);
    char* out = new char[size];
    if (!out) {
        return NULL;
    }
    ToUtf8(out, size, src);
    return out;
}

TCHAR* FromUtf8Alloc(const char* src)
{
    // A UTF-8 byte can never decode to more than one TCHAR on the device
    // (a 2-byte sequence yields 1 unit, a 4-byte sequence yields 2), and is
    // one-to-one on the host, so the byte count is always a safe upper bound.
    int bytes = src ? (int)strlen(src) : 0;
    TCHAR* out = new TCHAR[bytes + 1];
    if (!out) {
        return NULL;
    }
    FromUtf8(out, bytes + 1, src);
    return out;
}

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

Buffer::Buffer()
    : m_data(NULL)
    , m_len(0)
    , m_cap(0)
    , m_failed(false)
{
}

Buffer::~Buffer()
{
    delete[] m_data;
}

bool Buffer::Reserve(int extra)
{
    if (m_failed) {
        return false;
    }

    int needed = m_len + extra + 1;
    if (needed <= m_cap) {
        return true;
    }

    // Start at 256 TCHARs and double; most JSON documents and HTTP requests on
    // this device settle after one or two growths.
    int newCap = (m_cap > 0) ? m_cap : 256;
    while (newCap < needed) {
        if (newCap > (1 << 22)) { // ~4M TCHARs: far past anything legitimate
            m_failed = true;
            return false;
        }
        newCap *= 2;
    }

    TCHAR* grown = new TCHAR[newCap];
    if (!grown) {
        m_failed = true;
        return false;
    }

    for (int i = 0; i < m_len; i++) {
        grown[i] = m_data[i];
    }
    grown[m_len] = 0;

    delete[] m_data;
    m_data = grown;
    m_cap = newCap;

    return true;
}

bool Buffer::AppendN(const TCHAR* s, int len)
{
    if (!s || len <= 0) {
        return !m_failed;
    }
    if (!Reserve(len)) {
        return false;
    }

    for (int i = 0; i < len; i++) {
        m_data[m_len + i] = s[i];
    }
    m_len += len;
    m_data[m_len] = 0;

    return true;
}

bool Buffer::Append(const TCHAR* s)
{
    return AppendN(s, Str::Length(s));
}

bool Buffer::AppendChar(TCHAR ch)
{
    if (!Reserve(1)) {
        return false;
    }
    m_data[m_len++] = ch;
    m_data[m_len] = 0;
    return true;
}

bool Buffer::AppendInt(long value)
{
    TCHAR text[26];
    text[0] = 0;
    Str::AppendInt(text, 26, value);
    return Append(text);
}

bool Buffer::AppendJsonString(const TCHAR* s)
{
    if (!AppendChar((TCHAR)'"')) {
        return false;
    }

    // Escape in bounded chunks so an arbitrarily long value never needs an
    // arbitrarily large temporary.
    const int kChunk = 128;
    TCHAR escaped[kChunk * 6 + 1]; // worst case \u00XX per input character

    int i = 0;
    int len = Str::Length(s);
    while (i < len) {
        int take = (len - i > kChunk) ? kChunk : (len - i);

        TCHAR piece[kChunk + 1];
        Str::CopyN(piece, kChunk + 1, s + i, take);

        if (!Str::JsonEscape(escaped, kChunk * 6 + 1, piece)) {
            m_failed = true;
            return false;
        }
        if (!Append(escaped)) {
            return false;
        }

        i += take;
    }

    return AppendChar((TCHAR)'"');
}

bool Buffer::AppendJsonPair(const TCHAR* key, const TCHAR* value)
{
    if (!AppendJsonString(key)) {
        return false;
    }
    if (!AppendChar((TCHAR)':')) {
        return false;
    }
    return AppendJsonString(value);
}

bool Buffer::AppendJsonInt(const TCHAR* key, long value)
{
    if (!AppendJsonString(key)) {
        return false;
    }
    if (!AppendChar((TCHAR)':')) {
        return false;
    }
    return AppendInt(value);
}

const TCHAR* Buffer::Get() const
{
    static const TCHAR kEmpty[1] = { 0 };
    return m_data ? m_data : kEmpty;
}

int Buffer::Length() const
{
    return m_len;
}

bool Buffer::Failed() const
{
    return m_failed;
}

void Buffer::Clear()
{
    m_len = 0;
    if (m_data) {
        m_data[0] = 0;
    }
}

TCHAR* Buffer::Detach()
{
    if (m_failed) {
        return NULL;
    }
    if (!m_data) {
        // Callers expect an owned, empty string rather than NULL.
        TCHAR* empty = new TCHAR[1];
        if (empty) {
            empty[0] = 0;
        }
        return empty;
    }

    TCHAR* out = m_data;
    m_data = NULL;
    m_len = 0;
    m_cap = 0;

    return out;
}

} // namespace Str
} // namespace HBX
