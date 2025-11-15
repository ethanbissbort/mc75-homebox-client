#ifndef MODELS_JSONLITE_HPP
#define MODELS_JSONLITE_HPP

#include <windows.h>

namespace HBX {
namespace Models {

/**
 * Lightweight JSON parser for Windows Mobile
 * Minimal footprint for embedded environment
 */
class JsonLite {
public:
    JsonLite();
    ~JsonLite();

    // Parsing
    bool Parse(const TCHAR* jsonString);

    // Value extraction
    bool GetString(const TCHAR* key, TCHAR* value, DWORD maxLen) const;
    bool GetInt(const TCHAR* key, int* value) const;
    bool GetBool(const TCHAR* key, bool* value) const;
    bool GetDouble(const TCHAR* key, double* value) const;

    // Object/array handling
    bool HasKey(const TCHAR* key) const;
    bool IsArray() const;
    bool IsObject() const;
    int GetArrayLength() const;
    bool GetArrayElement(int index, JsonLite* element) const;

    // Building JSON
    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();
    void AddString(const TCHAR* key, const TCHAR* value);
    void AddInt(const TCHAR* key, int value);
    void AddBool(const TCHAR* key, bool value);
    void AddDouble(const TCHAR* key, double value);

    // Output
    TCHAR* ToString() const;
    void Clear();

private:
    struct Node {
        TCHAR* key;
        TCHAR* value;
        Node* next;
        Node* child;
        enum Type { TYPE_STRING, TYPE_INT, TYPE_BOOL, TYPE_DOUBLE, TYPE_OBJECT, TYPE_ARRAY, TYPE_NULL } type;
    };

    Node* m_root;
    TCHAR* m_parseBuffer;

    // Helper methods
    Node* CreateNode();
    void FreeNode(Node* node);
    Node* FindKey(const TCHAR* key) const;
    bool ParseValue(const TCHAR** ptr, Node* node);
    bool ParseObject(const TCHAR** ptr, Node* node);
    bool ParseArray(const TCHAR** ptr, Node* node);
    bool ParseString(const TCHAR** ptr, TCHAR** out);
    void SkipWhitespace(const TCHAR** ptr);
    void BuildString(const Node* node, TCHAR** buffer, int* pos, int maxLen) const;
};

} // namespace Models
} // namespace HBX

#endif // MODELS_JSONLITE_HPP
