#ifndef MODELS_JSONLITE_HPP
#define MODELS_JSONLITE_HPP

#include <windows.h>

#include "../StrUtil.hpp"

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

    /**
     * Nesting limit for Parse(). The parser descends recursively into objects
     * and arrays, and the MC75 gives each thread a small stack, so a malformed
     * or hostile response must not be able to pick the depth. Real HomeBox
     * payloads nest three or four levels.
     */
    enum { MAX_PARSE_DEPTH = 64 };

    // Parsing
    bool Parse(const TCHAR* jsonString);

    // Value extraction
    /**
     * Copies the value of `key` into `value`. Returns false if the key is
     * missing, is not a string, or the value did not fit in `maxLen` TCHARs --
     * a truncated field used to be reported as success, which silently wrote a
     * shortened value back to the server on the next update.
     */
    bool GetString(const TCHAR* key, TCHAR* value, DWORD maxLen) const;

    /**
     * Heap copy of the value of `key`, or NULL if it is missing or not a
     * string. Caller must delete[] the result. Use this instead of GetString
     * when the value length is not known in advance.
     */
    TCHAR* GetStringAlloc(const TCHAR* key) const;

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
    /** Serialized document; caller must delete[]. NULL if nothing was built. */
    TCHAR* ToString() const;
    void Clear();

    /**
     * Decodes one JSON string literal. `*ptr` must point at the opening quote;
     * on success it is advanced past the closing quote and `*out` receives a
     * heap copy (caller delete[]s) with all escape sequences -- including
     * \uXXXX and surrogate pairs -- resolved. Exposed so the hand-rolled
     * scanner in Config can decode values the same way the parser does.
     */
    static bool DecodeStringLiteral(const TCHAR** ptr, TCHAR** out);

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
    // When true, m_root points into another JsonLite's tree (see
    // GetArrayElement) and must not be freed by this instance.
    bool m_borrowedRoot;
    int m_depth;

    // Helper methods
    Node* CreateNode();
    void FreeNode(Node* node);
    Node* FindKey(const TCHAR* key) const;
    bool ParseValue(const TCHAR** ptr, Node* node);
    bool ParseObject(const TCHAR** ptr, Node* node);
    bool ParseArray(const TCHAR** ptr, Node* node);
    bool ParseString(const TCHAR** ptr, TCHAR** out);
    static void SkipWhitespace(const TCHAR** ptr);
    void BuildString(const Node* node, Str::Buffer& out) const;
};

} // namespace Models
} // namespace HBX

#endif // MODELS_JSONLITE_HPP
