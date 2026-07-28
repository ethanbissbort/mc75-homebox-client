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

    /**
     * Borrows the object stored under `key` into `out`.
     *
     * Nested reads are unavoidable for NetBox: every foreign key comes back as
     * a sub-object (`site`, `location`, `rack`, `device_type`, `role`,
     * `primary_ip`), `status` and `face` arrive as {"value","label"} pairs, and
     * a list response wraps everything in {"count","next","results":[...]}.
     *
     * `out` points into this parser's tree and never owns nodes, exactly like
     * GetArrayElement: the two objects may be destroyed in either order, but
     * `out` must not be *read* after this parser is cleared, re-parsed or
     * destroyed. A borrowed view can be descended into further, so
     * device -> device_type -> manufacturer -> name is three chained calls.
     *
     * Returns false when the key is missing or is not an object -- notably for
     * `"rack": null`, which is what an unracked device carries. `out` must be a
     * fresh or already-borrowing view: descending into this instance, or into
     * the parser that owns the tree, is refused rather than freeing the node
     * being handed over.
     */
    bool GetObject(const TCHAR* key, JsonLite* out) const;

    /** As GetObject, for an array member such as the `results` envelope. */
    bool GetArray(const TCHAR* key, JsonLite* out) const;

    /**
     * Copies `key`->`subKey` (e.g. site->name, status->value) into `out`.
     * Returns false -- leaving `out` untouched -- if either level is missing,
     * is null, or the value did not fit, so an unracked device reads as "no
     * rack name" rather than as a stale one.
     */
    bool GetNestedString(const TCHAR* key, const TCHAR* subKey, TCHAR* out, int outMax) const;

    /** Heap copy of `key`->`subKey`, or NULL. Caller must delete[]. */
    TCHAR* GetNestedStringAlloc(const TCHAR* key, const TCHAR* subKey) const;

    /**
     * Reads `key`->`subKey` as an integer, for the ids a write path needs:
     * a PATCH addresses related objects by bare id, while a read only ever
     * returns them nested.
     */
    bool GetNestedInt(const TCHAR* key, const TCHAR* subKey, int* value) const;

    // ---- member enumeration ----------------------------------------------
    //
    // The accessors above all address a member by name, which is enough for a
    // reader that knows the schema. Walking an object is for the writer that
    // does not: Config rewrites hb_conf.json from the fields it owns, and has to
    // carry every other key in the file through untouched rather than delete it.

    /** Members of this object, in document order; 0 if this is not an object. */
    int GetMemberCount() const;

    /**
     * Name of member `index`, or NULL when the index is out of range or this is
     * not an object. The pointer belongs to this parser's tree, so it follows
     * the same rule as a borrowed view: it must not be read after the parser is
     * cleared, re-parsed or destroyed. Copy it if it has to outlive that.
     */
    const TCHAR* GetMemberName(int index) const;

    /**
     * Member `index`'s value serialized back to JSON text -- quotes, escapes,
     * and the whole subtree for a nested object or array -- so a caller can
     * re-emit a value whose type it does not know. Caller must delete[]. NULL
     * when the index is out of range, this is not an object, or serialization
     * ran out of memory.
     */
    TCHAR* GetMemberJson(int index) const;

    /** As GetObject, addressing the member by position instead of by name. */
    bool GetMemberValue(int index, JsonLite* out) const;

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
    Node* MemberAt(int index) const;
    // Points `out` at a node of this tree without transferring ownership.
    bool BorrowNode(Node* node, JsonLite* out) const;
    static bool ContainsNode(const Node* root, const Node* node);
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
