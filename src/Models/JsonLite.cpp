#include "../../include/Models/JsonLite.hpp"
#include <wchar.h>

namespace HBX {
namespace Models {

JsonLite::JsonLite()
    : m_root(NULL)
    , m_parseBuffer(NULL)
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
    int len = lstrlen(jsonString);
    m_parseBuffer = new TCHAR[len + 1];
    lstrcpy(m_parseBuffer, jsonString);

    // Create root node
    m_root = CreateNode();

    // Parse the root value
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

    // Copy value to output buffer
    int len = lstrlen(node->value);
    if (len >= (int)maxLen) {
        len = maxLen - 1;
    }

    for (int i = 0; i < len; i++) {
        value[i] = node->value[i];
    }
    value[len] = '\0';

    return true;
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

    // Convert string to int
    *value = 0;
    bool negative = false;
    const TCHAR* ptr = node->value;

    if (*ptr == '-') {
        negative = true;
        ptr++;
    }

    while (*ptr >= '0' && *ptr <= '9') {
        *value = (*value * 10) + (*ptr - '0');
        ptr++;
    }

    if (negative) {
        *value = -*value;
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

    // Simple double parsing
    *value = 0.0;
    bool negative = false;
    const TCHAR* ptr = node->value;

    if (*ptr == '-') {
        negative = true;
        ptr++;
    }

    // Integer part
    while (*ptr >= '0' && *ptr <= '9') {
        *value = (*value * 10.0) + (*ptr - '0');
        ptr++;
    }

    // Fractional part
    if (*ptr == '.') {
        ptr++;
        double fraction = 0.1;
        while (*ptr >= '0' && *ptr <= '9') {
            *value += (*ptr - '0') * fraction;
            fraction *= 0.1;
            ptr++;
        }
    }

    if (negative) {
        *value = -*value;
    }

    return true;
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

    // Create a shallow reference to this node
    // Note: This is a simplified implementation
    // In a full implementation, we'd clone the node
    element->Clear();
    element->m_root = current;

    return true;
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

    // Set key
    int keyLen = lstrlen(key) + 1;
    newNode->key = new TCHAR[keyLen];
    lstrcpy(newNode->key, key);

    // Set value
    if (value) {
        int valueLen = lstrlen(value) + 1;
        newNode->value = new TCHAR[valueLen];
        lstrcpy(newNode->value, value);
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

    // Set key
    int keyLen = lstrlen(key) + 1;
    newNode->key = new TCHAR[keyLen];
    lstrcpy(newNode->key, key);

    // Set value (convert int to string)
    newNode->value = new TCHAR[32];
    wsprintf(newNode->value, TEXT("%d"), value);

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

    // Set key
    int keyLen = lstrlen(key) + 1;
    newNode->key = new TCHAR[keyLen];
    lstrcpy(newNode->key, key);

    // Set value
    newNode->value = new TCHAR[6];
    lstrcpy(newNode->value, value ? TEXT("true") : TEXT("false"));

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

    // Set key
    int keyLen = lstrlen(key) + 1;
    newNode->key = new TCHAR[keyLen];
    lstrcpy(newNode->key, key);

    // Set value (convert double to string)
    newNode->value = new TCHAR[64];
    // Simple double formatting (Windows Mobile doesn't have swprintf for doubles)
    int intPart = (int)value;
    double fracPart = value - intPart;
    if (fracPart < 0) fracPart = -fracPart;
    int fracDigits = (int)(fracPart * 1000000) % 1000000;
    wsprintf(newNode->value, TEXT("%d.%06d"), intPart, fracDigits);

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

    // Allocate buffer for output
    TCHAR* buffer = new TCHAR[8192];
    int pos = 0;

    BuildString(m_root, &buffer, &pos, 8192);

    return buffer;
}

void JsonLite::Clear()
{
    if (m_root) {
        FreeNode(m_root);
        m_root = NULL;
    }
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
    if (!node) return;
    
    if (node->key) delete[] node->key;
    if (node->value) delete[] node->value;
    if (node->next) FreeNode(node->next);
    if (node->child) FreeNode(node->child);
    
    delete node;
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
    else if (**ptr == '{') {
        // Object
        node->type = Node::TYPE_OBJECT;
        return ParseObject(ptr, node);
    }
    else if (**ptr == '[') {
        // Array
        node->type = Node::TYPE_ARRAY;
        return ParseArray(ptr, node);
    }
    else if (**ptr == 't' || **ptr == 'f') {
        // Boolean
        node->type = Node::TYPE_BOOL;
        if (wcsncmp(*ptr, TEXT("true"), 4) == 0) {
            node->value = new TCHAR[5];
            lstrcpy(node->value, TEXT("true"));
            *ptr += 4;
            return true;
        } else if (wcsncmp(*ptr, TEXT("false"), 5) == 0) {
            node->value = new TCHAR[6];
            lstrcpy(node->value, TEXT("false"));
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
        int len = *ptr - start;
        node->value = new TCHAR[len + 1];
        for (int i = 0; i < len; i++) {
            node->value[i] = start[i];
        }
        node->value[len] = '\0';

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

bool JsonLite::ParseString(const TCHAR** ptr, TCHAR** out)
{
    if (!ptr || !*ptr || !out) {
        return false;
    }

    SkipWhitespace(ptr);

    if (**ptr != '"') {
        return false;
    }
    (*ptr)++; // Skip opening quote

    // Find closing quote and calculate length
    const TCHAR* start = *ptr;
    const TCHAR* end = start;
    int len = 0;

    while (*end && *end != '"') {
        if (*end == '\\' && *(end + 1)) {
            end++; // Skip escaped character
        }
        end++;
        len++;
    }

    if (*end != '"') {
        return false; // No closing quote
    }

    // Allocate and copy string
    *out = new TCHAR[len + 1];
    int outPos = 0;
    const TCHAR* current = start;

    while (current < end) {
        if (*current == '\\' && current + 1 < end) {
            current++; // Skip backslash
            // Handle escape sequences
            switch (*current) {
                case 'n': (*out)[outPos++] = '\n'; break;
                case 'r': (*out)[outPos++] = '\r'; break;
                case 't': (*out)[outPos++] = '\t'; break;
                case '"': (*out)[outPos++] = '"'; break;
                case '\\': (*out)[outPos++] = '\\'; break;
                default: (*out)[outPos++] = *current; break;
            }
        } else {
            (*out)[outPos++] = *current;
        }
        current++;
    }
    (*out)[outPos] = '\0';

    *ptr = end + 1; // Skip closing quote
    return true;
}

void JsonLite::SkipWhitespace(const TCHAR** ptr)
{
    while (**ptr == ' ' || **ptr == '\t' || **ptr == '\r' || **ptr == '\n') {
        (*ptr)++;
    }
}

void JsonLite::BuildString(const Node* node, TCHAR** buffer, int* pos, int maxLen) const
{
    if (!node || !buffer || !*buffer || !pos) {
        return;
    }

    TCHAR* buf = *buffer;

    switch (node->type) {
        case Node::TYPE_OBJECT:
            if (*pos < maxLen) buf[(*pos)++] = '{';
            {
                Node* child = node->child;
                bool first = true;
                while (child) {
                    if (!first && *pos < maxLen) {
                        buf[(*pos)++] = ',';
                    }
                    first = false;

                    // Add key
                    if (child->key && *pos < maxLen - 2) {
                        buf[(*pos)++] = '"';
                        int keyLen = lstrlen(child->key);
                        for (int i = 0; i < keyLen && *pos < maxLen; i++) {
                            buf[(*pos)++] = child->key[i];
                        }
                        if (*pos < maxLen) buf[(*pos)++] = '"';
                        if (*pos < maxLen) buf[(*pos)++] = ':';
                    }

                    // Add value
                    BuildString(child, buffer, pos, maxLen);

                    child = child->next;
                }
            }
            if (*pos < maxLen) buf[(*pos)++] = '}';
            break;

        case Node::TYPE_ARRAY:
            if (*pos < maxLen) buf[(*pos)++] = '[';
            {
                Node* child = node->child;
                bool first = true;
                while (child) {
                    if (!first && *pos < maxLen) {
                        buf[(*pos)++] = ',';
                    }
                    first = false;

                    BuildString(child, buffer, pos, maxLen);

                    child = child->next;
                }
            }
            if (*pos < maxLen) buf[(*pos)++] = ']';
            break;

        case Node::TYPE_STRING:
            if (*pos < maxLen) buf[(*pos)++] = '"';
            if (node->value) {
                int valueLen = lstrlen(node->value);
                for (int i = 0; i < valueLen && *pos < maxLen; i++) {
                    buf[(*pos)++] = node->value[i];
                }
            }
            if (*pos < maxLen) buf[(*pos)++] = '"';
            break;

        case Node::TYPE_INT:
        case Node::TYPE_DOUBLE:
        case Node::TYPE_BOOL:
            if (node->value) {
                int valueLen = lstrlen(node->value);
                for (int i = 0; i < valueLen && *pos < maxLen; i++) {
                    buf[(*pos)++] = node->value[i];
                }
            }
            break;

        case Node::TYPE_NULL:
            if (*pos < maxLen - 4) {
                buf[(*pos)++] = 'n';
                buf[(*pos)++] = 'u';
                buf[(*pos)++] = 'l';
                buf[(*pos)++] = 'l';
            }
            break;
    }

    if (*pos < maxLen) {
        buf[*pos] = '\0';
    }
}

} // namespace Models
} // namespace HBX
