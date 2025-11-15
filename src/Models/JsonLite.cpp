#include "../../include/Models/JsonLite.hpp"

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
    // TODO: Implement JSON parsing
    return false;
}

bool JsonLite::GetString(const TCHAR* key, TCHAR* value, DWORD maxLen) const
{
    // TODO: Implement
    return false;
}

bool JsonLite::GetInt(const TCHAR* key, int* value) const
{
    // TODO: Implement
    return false;
}

bool JsonLite::GetBool(const TCHAR* key, bool* value) const
{
    // TODO: Implement
    return false;
}

bool JsonLite::GetDouble(const TCHAR* key, double* value) const
{
    // TODO: Implement
    return false;
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
    // TODO: Implement
    return 0;
}

bool JsonLite::GetArrayElement(int index, JsonLite* element) const
{
    // TODO: Implement
    return false;
}

void JsonLite::BeginObject()
{
    // TODO: Implement
}

void JsonLite::EndObject()
{
    // TODO: Implement
}

void JsonLite::BeginArray()
{
    // TODO: Implement
}

void JsonLite::EndArray()
{
    // TODO: Implement
}

void JsonLite::AddString(const TCHAR* key, const TCHAR* value)
{
    // TODO: Implement
}

void JsonLite::AddInt(const TCHAR* key, int value)
{
    // TODO: Implement
}

void JsonLite::AddBool(const TCHAR* key, bool value)
{
    // TODO: Implement
}

void JsonLite::AddDouble(const TCHAR* key, double value)
{
    // TODO: Implement
}

TCHAR* JsonLite::ToString() const
{
    // TODO: Implement
    return NULL;
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
    // TODO: Implement
    return NULL;
}

bool JsonLite::ParseValue(const TCHAR** ptr, Node* node)
{
    // TODO: Implement
    return false;
}

bool JsonLite::ParseObject(const TCHAR** ptr, Node* node)
{
    // TODO: Implement
    return false;
}

bool JsonLite::ParseArray(const TCHAR** ptr, Node* node)
{
    // TODO: Implement
    return false;
}

bool JsonLite::ParseString(const TCHAR** ptr, TCHAR** out)
{
    // TODO: Implement
    return false;
}

void JsonLite::SkipWhitespace(const TCHAR** ptr)
{
    while (**ptr == ' ' || **ptr == '\t' || **ptr == '\r' || **ptr == '\n') {
        (*ptr)++;
    }
}

void JsonLite::BuildString(const Node* node, TCHAR** buffer, int* pos, int maxLen) const
{
    // TODO: Implement
}

} // namespace Models
} // namespace HBX
