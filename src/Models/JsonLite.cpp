#include "../../include/Models/JsonLite.hpp"
#include "../../include/StrUtil.hpp"
#include <wchar.h>
#include <limits.h>

namespace HBX {
namespace Models {

namespace {

// ---------------------------------------------------------------------------
// Number scanning
//
// The MC75 CRT has no swscanf/strtod for TCHAR text worth linking in, so the
// numeric token stored by ParseValue is decoded by hand. It must accept
// everything ParseValue accepts -- including the exponent -- or the accessors
// silently disagree with the parser.
// ---------------------------------------------------------------------------

bool IsDigit(TCHAR c)
{
    return c >= (TCHAR)'0' && c <= (TCHAR)'9';
}

double ScalePow10(double value, int exponent)
{
    // A JSON exponent may have any number of digits; clamp before looping so a
    // hostile "1e99999999" cannot spin the UI thread.
    if (exponent > 400) {
        exponent = 400;
    } else if (exponent < -400) {
        exponent = -400;
    }

    while (exponent > 0) {
        value *= 10.0;
        exponent--;
    }
    while (exponent < 0) {
        value /= 10.0;
        exponent++;
    }

    return value;
}

bool ParseNumberToken(const TCHAR* s, double* out)
{
    if (!s || !out) {
        return false;
    }

    const TCHAR* p = s;
    bool negative = false;

    if (*p == (TCHAR)'-') {
        negative = true;
        p++;
    } else if (*p == (TCHAR)'+') {
        p++;
    }

    bool anyDigit = false;
    double value = 0.0;

    while (IsDigit(*p)) {
        value = value * 10.0 + (double)(*p - (TCHAR)'0');
        p++;
        anyDigit = true;
    }

    if (*p == (TCHAR)'.') {
        p++;
        double scale = 0.1;
        while (IsDigit(*p)) {
            value += (double)(*p - (TCHAR)'0') * scale;
            scale *= 0.1;
            p++;
            anyDigit = true;
        }
    }

    if (!anyDigit) {
        return false;
    }

    if (*p == (TCHAR)'e' || *p == (TCHAR)'E') {
        p++;
        bool exponentNegative = false;
        if (*p == (TCHAR)'-') {
            exponentNegative = true;
            p++;
        } else if (*p == (TCHAR)'+') {
            p++;
        }

        int exponent = 0;
        bool anyExponentDigit = false;
        while (IsDigit(*p)) {
            if (exponent < 100000) {
                exponent = exponent * 10 + (int)(*p - (TCHAR)'0');
            }
            p++;
            anyExponentDigit = true;
        }

        if (!anyExponentDigit) {
            return false;
        }

        value = ScalePow10(value, exponentNegative ? -exponent : exponent);
    }

    *out = negative ? -value : value;
    return true;
}

// ---------------------------------------------------------------------------
// String escape decoding
// ---------------------------------------------------------------------------

bool HexDigit(TCHAR c, unsigned long* out)
{
    if (c >= (TCHAR)'0' && c <= (TCHAR)'9') {
        *out = (unsigned long)(c - (TCHAR)'0');
        return true;
    }
    if (c >= (TCHAR)'a' && c <= (TCHAR)'f') {
        *out = (unsigned long)(c - (TCHAR)'a') + 10UL;
        return true;
    }
    if (c >= (TCHAR)'A' && c <= (TCHAR)'F') {
        *out = (unsigned long)(c - (TCHAR)'A') + 10UL;
        return true;
    }
    return false;
}

// Reads exactly four hex digits at `p`, which must stay below `end`.
bool ReadHex4(const TCHAR* p, const TCHAR* end, unsigned long* out)
{
    if (p + 4 > end) {
        return false;
    }

    unsigned long value = 0;
    for (int i = 0; i < 4; i++) {
        unsigned long digit;
        if (!HexDigit(p[i], &digit)) {
            return false;
        }
        value = (value << 4) | digit;
    }

    *out = value;
    return true;
}

// Writes one Unicode scalar in whatever encoding a TCHAR uses on this build:
// UTF-16 code units on the device, UTF-8 bytes on the host (where the rest of
// the codebase already treats TCHAR text as UTF-8 bytes).
void EmitScalar(TCHAR* out, int* pos, unsigned long scalar)
{
    if (sizeof(TCHAR) > 1) {
        if (scalar > 0xFFFFUL) {
            unsigned long v = scalar - 0x10000UL;
            out[(*pos)++] = (TCHAR)(unsigned short)(0xD800UL + (v >> 10));
            out[(*pos)++] = (TCHAR)(unsigned short)(0xDC00UL + (v & 0x3FFUL));
        } else {
            out[(*pos)++] = (TCHAR)(unsigned short)scalar;
        }
        return;
    }

    if (scalar < 0x80UL) {
        out[(*pos)++] = (TCHAR)(unsigned char)scalar;
    } else if (scalar < 0x800UL) {
        out[(*pos)++] = (TCHAR)(unsigned char)(0xC0UL | (scalar >> 6));
        out[(*pos)++] = (TCHAR)(unsigned char)(0x80UL | (scalar & 0x3FUL));
    } else if (scalar < 0x10000UL) {
        out[(*pos)++] = (TCHAR)(unsigned char)(0xE0UL | (scalar >> 12));
        out[(*pos)++] = (TCHAR)(unsigned char)(0x80UL | ((scalar >> 6) & 0x3FUL));
        out[(*pos)++] = (TCHAR)(unsigned char)(0x80UL | (scalar & 0x3FUL));
    } else {
        out[(*pos)++] = (TCHAR)(unsigned char)(0xF0UL | (scalar >> 18));
        out[(*pos)++] = (TCHAR)(unsigned char)(0x80UL | ((scalar >> 12) & 0x3FUL));
        out[(*pos)++] = (TCHAR)(unsigned char)(0x80UL | ((scalar >> 6) & 0x3FUL));
        out[(*pos)++] = (TCHAR)(unsigned char)(0x80UL | (scalar & 0x3FUL));
    }
}

} // namespace

JsonLite::JsonLite()
    : m_root(NULL)
    , m_parseBuffer(NULL)
    , m_borrowedRoot(false)
    , m_depth(0)
{
}

JsonLite::~JsonLite()
{
    Clear();
}

bool JsonLite::Parse(const TCHAR* jsonString)
{
    if (!jsonString) {
        return false;
    }

    Clear();

    // Create a copy of the JSON string for parsing
    m_parseBuffer = Str::Dup(jsonString);
    if (!m_parseBuffer) {
        return false;
    }

    // Create root node
    m_root = CreateNode();

    // Parse the root value
    m_depth = 0;
    const TCHAR* ptr = m_parseBuffer;
    return ParseValue(&ptr, m_root);
}

bool JsonLite::GetString(const TCHAR* key, TCHAR* value, DWORD maxLen) const
{
    if (!key || !value || maxLen == 0) {
        return false;
    }

    Node* node = FindKey(key);
    if (!node || node->type != Node::TYPE_STRING || !node->value) {
        return false;
    }

    // Truncation is a failure, not a success: a shortened value that reads as
    // "extracted fine" ends up written back over the server's full field.
    return Str::Copy(value, (int)maxLen, node->value);
}

TCHAR* JsonLite::GetStringAlloc(const TCHAR* key) const
{
    if (!key) {
        return NULL;
    }

    Node* node = FindKey(key);
    if (!node || node->type != Node::TYPE_STRING || !node->value) {
        return NULL;
    }

    return Str::Dup(node->value);
}

bool JsonLite::GetInt(const TCHAR* key, int* value) const
{
    if (!key || !value) {
        return false;
    }

    Node* node = FindKey(key);
    if (!node || (node->type != Node::TYPE_INT && node->type != Node::TYPE_DOUBLE) || !node->value) {
        return false;
    }

    double parsed = 0.0;
    if (!ParseNumberToken(node->value, &parsed)) {
        return false;
    }

    // Saturate rather than wrap: an out-of-range quantity from the server must
    // not turn into a small negative number on the device.
    if (parsed >= (double)INT_MAX) {
        *value = INT_MAX;
    } else if (parsed <= (double)INT_MIN) {
        *value = INT_MIN;
    } else {
        *value = (int)parsed;
    }

    return true;
}

bool JsonLite::GetBool(const TCHAR* key, bool* value) const
{
    if (!key || !value) {
        return false;
    }

    Node* node = FindKey(key);
    if (!node || node->type != Node::TYPE_BOOL || !node->value) {
        return false;
    }

    *value = (lstrcmp(node->value, TEXT("true")) == 0);
    return true;
}

bool JsonLite::GetDouble(const TCHAR* key, double* value) const
{
    if (!key || !value) {
        return false;
    }

    Node* node = FindKey(key);
    if (!node || (node->type != Node::TYPE_DOUBLE && node->type != Node::TYPE_INT) || !node->value) {
        return false;
    }

    return ParseNumberToken(node->value, value);
}

bool JsonLite::HasKey(const TCHAR* key) const
{
    return FindKey(key) != NULL;
}

bool JsonLite::IsArray() const
{
    return m_root && m_root->type == Node::TYPE_ARRAY;
}

bool JsonLite::IsObject() const
{
    return m_root && m_root->type == Node::TYPE_OBJECT;
}

int JsonLite::GetArrayLength() const
{
    if (!m_root || m_root->type != Node::TYPE_ARRAY) {
        return 0;
    }

    int count = 0;
    Node* current = m_root->child;
    while (current) {
        count++;
        current = current->next;
    }

    return count;
}

bool JsonLite::GetArrayElement(int index, JsonLite* element) const
{
    if (!element || index < 0 || !m_root || m_root->type != Node::TYPE_ARRAY) {
        return false;
    }

    // Find the element at the given index
    Node* current = m_root->child;
    int currentIndex = 0;

    while (current && currentIndex < index) {
        current = current->next;
        currentIndex++;
    }

    if (!current || currentIndex != index) {
        return false; // Index out of bounds
    }

    return BorrowNode(current, element);
}

bool JsonLite::GetObject(const TCHAR* key, JsonLite* out) const
{
    Node* node = FindKey(key);
    if (!node || node->type != Node::TYPE_OBJECT) {
        return false;
    }

    return BorrowNode(node, out);
}

bool JsonLite::GetArray(const TCHAR* key, JsonLite* out) const
{
    Node* node = FindKey(key);
    if (!node || node->type != Node::TYPE_ARRAY) {
        return false;
    }

    return BorrowNode(node, out);
}

bool JsonLite::GetNestedString(const TCHAR* key, const TCHAR* subKey,
                               TCHAR* out, int outMax) const
{
    if (!out || outMax <= 0) {
        return false;
    }

    // The borrowed view holds two pointers and allocates nothing, so this stays
    // free of heap traffic on the scan path even though it is called once per
    // displayed field.
    JsonLite child;
    if (!GetObject(key, &child)) {
        return false;
    }

    return child.GetString(subKey, out, (DWORD)outMax);
}

TCHAR* JsonLite::GetNestedStringAlloc(const TCHAR* key, const TCHAR* subKey) const
{
    JsonLite child;
    if (!GetObject(key, &child)) {
        return NULL;
    }

    return child.GetStringAlloc(subKey);
}

bool JsonLite::GetNestedInt(const TCHAR* key, const TCHAR* subKey, int* value) const
{
    JsonLite child;
    if (!GetObject(key, &child)) {
        return false;
    }

    return child.GetInt(subKey, value);
}

int JsonLite::GetMemberCount() const
{
    if (!m_root || m_root->type != Node::TYPE_OBJECT) {
        return 0;
    }

    int count = 0;
    Node* current = m_root->child;
    while (current) {
        count++;
        current = current->next;
    }

    return count;
}

const TCHAR* JsonLite::GetMemberName(int index) const
{
    Node* node = MemberAt(index);
    return node ? node->key : NULL;
}

TCHAR* JsonLite::GetMemberJson(int index) const
{
    Node* node = MemberAt(index);
    if (!node) {
        return NULL;
    }

    // The same growable serializer ToString() uses, run over one member instead
    // of the whole document: a value of any shape comes back as text that parses
    // to what was read, without this having to know the shape.
    Str::Buffer out;
    BuildString(node, out);

    if (out.Failed()) {
        return NULL;
    }

    return out.Detach();
}

bool JsonLite::GetMemberValue(int index, JsonLite* out) const
{
    Node* node = MemberAt(index);
    if (!node) {
        return false;
    }

    return BorrowNode(node, out);
}

void JsonLite::BeginObject()
{
    Clear();
    m_root = CreateNode();
    m_root->type = Node::TYPE_OBJECT;
}

void JsonLite::EndObject()
{
    // Nothing to do - object is already complete
    // This method exists for API symmetry
}

void JsonLite::BeginArray()
{
    Clear();
    m_root = CreateNode();
    m_root->type = Node::TYPE_ARRAY;
}

void JsonLite::EndArray()
{
    // Nothing to do - array is already complete
    // This method exists for API symmetry
}

void JsonLite::AddString(const TCHAR* key, const TCHAR* value)
{
    if (!m_root || !key) {
        return;
    }

    Node* newNode = CreateNode();
    newNode->type = Node::TYPE_STRING;
    newNode->key = Str::Dup(key);
    if (value) {
        newNode->value = Str::Dup(value);
    }

    // Add to children list
    if (!m_root->child) {
        m_root->child = newNode;
    } else {
        Node* current = m_root->child;
        while (current->next) {
            current = current->next;
        }
        current->next = newNode;
    }
}

void JsonLite::AddInt(const TCHAR* key, int value)
{
    if (!m_root || !key) {
        return;
    }

    Node* newNode = CreateNode();
    newNode->type = Node::TYPE_INT;
    newNode->key = Str::Dup(key);

    TCHAR text[32];
    text[0] = 0;
    Str::AppendInt(text, 32, value);
    newNode->value = Str::Dup(text);

    // Add to children list
    if (!m_root->child) {
        m_root->child = newNode;
    } else {
        Node* current = m_root->child;
        while (current->next) {
            current = current->next;
        }
        current->next = newNode;
    }
}

void JsonLite::AddBool(const TCHAR* key, bool value)
{
    if (!m_root || !key) {
        return;
    }

    Node* newNode = CreateNode();
    newNode->type = Node::TYPE_BOOL;
    newNode->key = Str::Dup(key);
    newNode->value = Str::Dup(value ? TEXT("true") : TEXT("false"));

    // Add to children list
    if (!m_root->child) {
        m_root->child = newNode;
    } else {
        Node* current = m_root->child;
        while (current->next) {
            current = current->next;
        }
        current->next = newNode;
    }
}

void JsonLite::AddDouble(const TCHAR* key, double value)
{
    if (!m_root || !key) {
        return;
    }

    Node* newNode = CreateNode();
    newNode->type = Node::TYPE_DOUBLE;
    newNode->key = Str::Dup(key);

    // The CE CRT's wsprintf has no floating point conversion at all, so the
    // fixed six-decimal form is assembled by hand. The sign has to be carried
    // separately: for -1 < value < 0 the integer part is 0 and would otherwise
    // lose it.
    bool negative = (value < 0.0);
    double magnitude = negative ? -value : value;

    // Values at or beyond the 32-bit range have no representable integer part
    // here; clamp instead of invoking undefined behaviour in the cast.
    if (magnitude >= 2147483647.0) {
        magnitude = 2147483647.0;
    }

    long intPart = (long)magnitude;
    long fracDigits = (long)((magnitude - (double)intPart) * 1000000.0 + 0.5);
    if (fracDigits >= 1000000) {
        // Rounding carried into the integer part (e.g. 0.9999999).
        fracDigits = 0;
        intPart++;
    }

    TCHAR text[64];
    text[0] = 0;
    if (negative) {
        Str::Append(text, 64, TEXT("-"));
    }
    Str::AppendInt(text, 64, intPart);
    Str::Append(text, 64, TEXT("."));
    for (long divisor = 100000; divisor > 0; divisor /= 10) {
        Str::AppendChar(text, 64, (TCHAR)('0' + (int)((fracDigits / divisor) % 10)));
    }

    newNode->value = Str::Dup(text);

    // Add to children list
    if (!m_root->child) {
        m_root->child = newNode;
    } else {
        Node* current = m_root->child;
        while (current->next) {
            current = current->next;
        }
        current->next = newNode;
    }
}

TCHAR* JsonLite::ToString() const
{
    if (!m_root) {
        return NULL;
    }

    // Growable, so a large document is serialized in full rather than cut off
    // mid-token at a fixed size and handed back as success.
    Str::Buffer out;
    BuildString(m_root, out);

    if (out.Failed()) {
        return NULL;
    }

    return out.Detach();
}

void JsonLite::Clear()
{
    if (m_root) {
        if (!m_borrowedRoot) {
            FreeNode(m_root);
        }
        m_root = NULL;
    }
    m_borrowedRoot = false;
    m_depth = 0;
    if (m_parseBuffer) {
        delete[] m_parseBuffer;
        m_parseBuffer = NULL;
    }
}

JsonLite::Node* JsonLite::CreateNode()
{
    Node* node = new Node();
    node->key = NULL;
    node->value = NULL;
    node->next = NULL;
    node->child = NULL;
    node->type = Node::TYPE_NULL;
    return node;
}

void JsonLite::FreeNode(Node* node)
{
    // Siblings are walked iteratively: recursing once per sibling would make
    // the teardown depth equal to the number of elements in an array, and a
    // flat response of a few hundred items is enough to exhaust the MC75
    // stack. Recursion only follows nesting, which Parse() caps at
    // MAX_PARSE_DEPTH.
    while (node) {
        Node* next = node->next;

        if (node->key) delete[] node->key;
        if (node->value) delete[] node->value;
        if (node->child) FreeNode(node->child);

        delete node;
        node = next;
    }
}

JsonLite::Node* JsonLite::FindKey(const TCHAR* key) const
{
    if (!m_root || !key) {
        return NULL;
    }

    // Search through child nodes (for objects)
    Node* current = m_root->child;
    while (current) {
        if (current->key && lstrcmp(current->key, key) == 0) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

JsonLite::Node* JsonLite::MemberAt(int index) const
{
    if (index < 0 || !m_root || m_root->type != Node::TYPE_OBJECT) {
        return NULL;
    }

    Node* current = m_root->child;
    int currentIndex = 0;

    while (current && currentIndex < index) {
        current = current->next;
        currentIndex++;
    }

    return current;
}

bool JsonLite::ContainsNode(const Node* root, const Node* node)
{
    // Siblings are walked iteratively for the same reason FreeNode does it: a
    // `results` array of a few hundred entries would otherwise recurse once per
    // element. Recursion follows nesting only, which Parse caps.
    while (root) {
        if (root == node) {
            return true;
        }
        if (root->child && ContainsNode(root->child, node)) {
            return true;
        }
        root = root->next;
    }

    return false;
}

bool JsonLite::BorrowNode(Node* node, JsonLite* out) const
{
    if (!node || !out) {
        return false;
    }

    // Two ways a descent can be asked to free the node it is handing over,
    // both of them use-after-free:
    //   - descending into oneself, and
    //   - descending into the parser that owns the tree (the shape a path walk
    //     falls into when it reuses the document as the destination).
    // Descend into a fresh view, or into one that is already borrowing.
    if (out == this) {
        return false;
    }
    if (out->m_root && !out->m_borrowedRoot && ContainsNode(out->m_root, node)) {
        return false;
    }

    out->Clear();
    out->m_root = node;
    // The node stays owned by this parser's tree; the view only borrows it and
    // must not free it in its destructor (that would double-free). Because a
    // borrowed view never frees anything, owner and view can be destroyed in
    // either order -- only reading through a stale view is unsafe.
    out->m_borrowedRoot = true;

    return true;
}

bool JsonLite::ParseValue(const TCHAR** ptr, Node* node)
{
    if (!ptr || !*ptr || !node) {
        return false;
    }

    SkipWhitespace(ptr);

    // Check value type
    if (**ptr == '"') {
        // String
        node->type = Node::TYPE_STRING;
        return ParseString(ptr, &node->value);
    }
    else if (**ptr == '{' || **ptr == '[') {
        // Object / array. The descent is bounded so a deeply nested (or
        // hostile) payload cannot exhaust the device stack.
        bool isObject = (**ptr == '{');
        node->type = isObject ? Node::TYPE_OBJECT : Node::TYPE_ARRAY;

        if (m_depth >= MAX_PARSE_DEPTH) {
            return false;
        }

        m_depth++;
        bool ok = isObject ? ParseObject(ptr, node) : ParseArray(ptr, node);
        m_depth--;

        return ok;
    }
    else if (**ptr == 't' || **ptr == 'f') {
        // Boolean
        node->type = Node::TYPE_BOOL;
        if (wcsncmp(*ptr, TEXT("true"), 4) == 0) {
            node->value = Str::Dup(TEXT("true"));
            *ptr += 4;
            return true;
        } else if (wcsncmp(*ptr, TEXT("false"), 5) == 0) {
            node->value = Str::Dup(TEXT("false"));
            *ptr += 5;
            return true;
        }
        return false;
    }
    else if (**ptr == 'n') {
        // Null
        node->type = Node::TYPE_NULL;
        if (wcsncmp(*ptr, TEXT("null"), 4) == 0) {
            *ptr += 4;
            return true;
        }
        return false;
    }
    else if (**ptr == '-' || (**ptr >= '0' && **ptr <= '9')) {
        // Number (int or double)
        const TCHAR* start = *ptr;
        bool isDouble = false;

        if (**ptr == '-') (*ptr)++;
        while (**ptr >= '0' && **ptr <= '9') (*ptr)++;

        if (**ptr == '.') {
            isDouble = true;
            (*ptr)++;
            while (**ptr >= '0' && **ptr <= '9') (*ptr)++;
        }

        if (**ptr == 'e' || **ptr == 'E') {
            isDouble = true;
            (*ptr)++;
            if (**ptr == '+' || **ptr == '-') (*ptr)++;
            while (**ptr >= '0' && **ptr <= '9') (*ptr)++;
        }

        // Extract number string
        node->value = Str::DupN(start, (int)(*ptr - start));

        node->type = isDouble ? Node::TYPE_DOUBLE : Node::TYPE_INT;
        return true;
    }

    return false;
}

bool JsonLite::ParseObject(const TCHAR** ptr, Node* node)
{
    if (!ptr || !*ptr || !node) {
        return false;
    }

    SkipWhitespace(ptr);

    if (**ptr != '{') {
        return false;
    }
    (*ptr)++; // Skip '{'

    SkipWhitespace(ptr);

    if (**ptr == '}') {
        // Empty object
        (*ptr)++;
        return true;
    }

    Node* lastChild = NULL;

    while (**ptr) {
        SkipWhitespace(ptr);

        // Parse key (must be a string)
        TCHAR* key = NULL;
        if (!ParseString(ptr, &key)) {
            return false;
        }

        SkipWhitespace(ptr);

        // Expect ':'
        if (**ptr != ':') {
            delete[] key;
            return false;
        }
        (*ptr)++;

        // Parse value
        Node* childNode = CreateNode();
        childNode->key = key;

        if (!ParseValue(ptr, childNode)) {
            FreeNode(childNode);
            return false;
        }

        // Add to children list
        if (!node->child) {
            node->child = childNode;
        } else {
            lastChild->next = childNode;
        }
        lastChild = childNode;

        SkipWhitespace(ptr);

        // Check for more pairs
        if (**ptr == ',') {
            (*ptr)++;
            continue;
        } else if (**ptr == '}') {
            (*ptr)++;
            return true;
        } else {
            return false; // Unexpected character
        }
    }

    return false; // Unexpected end
}

bool JsonLite::ParseArray(const TCHAR** ptr, Node* node)
{
    if (!ptr || !*ptr || !node) {
        return false;
    }

    SkipWhitespace(ptr);

    if (**ptr != '[') {
        return false;
    }
    (*ptr)++; // Skip '['

    SkipWhitespace(ptr);

    if (**ptr == ']') {
        // Empty array
        (*ptr)++;
        return true;
    }

    Node* lastChild = NULL;

    while (**ptr) {
        SkipWhitespace(ptr);

        // Parse array element
        Node* childNode = CreateNode();

        if (!ParseValue(ptr, childNode)) {
            FreeNode(childNode);
            return false;
        }

        // Add to children list
        if (!node->child) {
            node->child = childNode;
        } else {
            lastChild->next = childNode;
        }
        lastChild = childNode;

        SkipWhitespace(ptr);

        // Check for more elements
        if (**ptr == ',') {
            (*ptr)++;
            continue;
        } else if (**ptr == ']') {
            (*ptr)++;
            return true;
        } else {
            return false; // Unexpected character
        }
    }

    return false; // Unexpected end
}

bool JsonLite::DecodeStringLiteral(const TCHAR** ptr, TCHAR** out)
{
    if (!ptr || !*ptr || !out) {
        return false;
    }

    *out = NULL;

    if (**ptr != '"') {
        return false;
    }
    (*ptr)++; // Skip opening quote

    // Locate the closing quote, honouring backslash escapes.
    const TCHAR* start = *ptr;
    const TCHAR* end = start;

    while (*end && *end != '"') {
        if (*end == '\\' && *(end + 1)) {
            end++; // an escaped quote does not close the string
        }
        end++;
    }

    if (*end != '"') {
        return false; // No closing quote
    }

    // The decoded form is never longer than the raw span: every escape
    // sequence shrinks (\uXXXX is six source characters and at most three
    // UTF-8 bytes, a surrogate pair twelve source characters and four).
    int span = (int)(end - start);
    TCHAR* result = new TCHAR[span + 1];
    if (!result) {
        return false;
    }

    int outPos = 0;
    const TCHAR* current = start;
    bool ok = true;

    while (current < end) {
        if (*current != '\\') {
            result[outPos++] = *current++;
            continue;
        }

        if (current + 1 >= end) {
            ok = false; // trailing backslash before the closing quote
            break;
        }
        current++; // step onto the escape character

        switch (*current) {
        case 'n': result[outPos++] = (TCHAR)'\n'; current++; break;
        case 'r': result[outPos++] = (TCHAR)'\r'; current++; break;
        case 't': result[outPos++] = (TCHAR)'\t'; current++; break;
        case 'b': result[outPos++] = (TCHAR)'\b'; current++; break;
        case 'f': result[outPos++] = (TCHAR)'\f'; current++; break;
        case 'u': {
            unsigned long scalar = 0;
            if (!ReadHex4(current + 1, end, &scalar)) {
                ok = false;
                break;
            }
            current += 5; // 'u' plus four hex digits

            if (scalar >= 0xD800UL && scalar <= 0xDBFFUL &&
                current + 6 <= end && current[0] == '\\' && current[1] == 'u') {
                unsigned long low = 0;
                if (ReadHex4(current + 2, end, &low) &&
                    low >= 0xDC00UL && low <= 0xDFFFUL) {
                    scalar = 0x10000UL + ((scalar - 0xD800UL) << 10) + (low - 0xDC00UL);
                    current += 6;
                }
            }

            if (scalar >= 0xD800UL && scalar <= 0xDFFFUL) {
                scalar = 0xFFFDUL; // unpaired surrogate
            }

            EmitScalar(result, &outPos, scalar);
            break;
        }
        default:
            // '"', '\\', '/' and anything else: keep the character as written
            // rather than failing a whole response over one stray backslash.
            result[outPos++] = *current++;
            break;
        }

        if (!ok) {
            break;
        }
    }

    if (!ok) {
        delete[] result;
        return false;
    }

    result[outPos] = '\0';

    *out = result;
    *ptr = end + 1; // Skip closing quote
    return true;
}

bool JsonLite::ParseString(const TCHAR** ptr, TCHAR** out)
{
    if (!ptr || !*ptr || !out) {
        return false;
    }

    SkipWhitespace(ptr);

    return DecodeStringLiteral(ptr, out);
}

void JsonLite::SkipWhitespace(const TCHAR** ptr)
{
    while (**ptr == ' ' || **ptr == '\t' || **ptr == '\r' || **ptr == '\n') {
        (*ptr)++;
    }
}

void JsonLite::BuildString(const Node* node, Str::Buffer& out) const
{
    if (!node) {
        return;
    }

    switch (node->type) {
        case Node::TYPE_OBJECT: {
            out.AppendChar((TCHAR)'{');
            Node* child = node->child;
            bool first = true;
            while (child) {
                if (!first) {
                    out.AppendChar((TCHAR)',');
                }
                first = false;

                if (child->key) {
                    out.AppendJsonString(child->key);
                    out.AppendChar((TCHAR)':');
                }

                BuildString(child, out);
                child = child->next;
            }
            out.AppendChar((TCHAR)'}');
            break;
        }

        case Node::TYPE_ARRAY: {
            out.AppendChar((TCHAR)'[');
            Node* child = node->child;
            bool first = true;
            while (child) {
                if (!first) {
                    out.AppendChar((TCHAR)',');
                }
                first = false;

                BuildString(child, out);
                child = child->next;
            }
            out.AppendChar((TCHAR)']');
            break;
        }

        case Node::TYPE_STRING:
            // Escaped on the way out, so a value holding a quote, a backslash
            // or a newline still yields a document the server can parse and
            // the line-oriented journal can queue.
            out.AppendJsonString(node->value);
            break;

        case Node::TYPE_INT:
        case Node::TYPE_DOUBLE:
        case Node::TYPE_BOOL:
            out.Append(node->value ? node->value : TEXT("null"));
            break;

        case Node::TYPE_NULL:
            out.Append(TEXT("null"));
            break;
    }
}

} // namespace Models
} // namespace HBX
