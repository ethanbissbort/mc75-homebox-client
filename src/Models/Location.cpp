#include "../../include/Models/Location.hpp"
#include "../../include/Models/JsonLite.hpp"
#include "../../include/StrUtil.hpp"

namespace HBX {
namespace Models {

namespace {

// Appends `"key":"escaped value"` to an object under construction, inserting
// the separating comma only when something has already been written.
void AppendField(Str::Buffer& out, bool* first, const TCHAR* key, const TCHAR* value)
{
    if (!value) {
        return;
    }
    if (!*first) {
        out.AppendChar((TCHAR)',');
    }
    *first = false;
    out.AppendJsonPair(key, value);
}

// Reads one string field at its full length; extracting through a shared fixed
// buffer would silently cap every field.
void AssignField(const JsonLite& parser, const TCHAR* key, Location* location,
                 void (Location::*setter)(const TCHAR*))
{
    TCHAR* value = parser.GetStringAlloc(key);
    if (!value) {
        return;
    }
    (location->*setter)(value);
    delete[] value;
}

} // namespace

Location::Location()
    : m_id(NULL)
    , m_name(NULL)
    , m_description(NULL)
    , m_parentId(NULL)
    , m_path(NULL)
{
}

Location::~Location()
{
    Cleanup();
}

void Location::Cleanup()
{
    if (m_id) delete[] m_id;
    if (m_name) delete[] m_name;
    if (m_description) delete[] m_description;
    if (m_parentId) delete[] m_parentId;
    if (m_path) delete[] m_path;
    
    m_id = m_name = m_description = m_parentId = m_path = NULL;
}

const TCHAR* Location::GetId() const { return m_id; }
const TCHAR* Location::GetName() const { return m_name; }
const TCHAR* Location::GetDescription() const { return m_description; }
const TCHAR* Location::GetParentId() const { return m_parentId; }
const TCHAR* Location::GetPath() const { return m_path; }

void Location::SetId(const TCHAR* id)
{
    if (m_id) delete[] m_id;
    if (id) {
        m_id = new TCHAR[lstrlen(id) + 1];
        lstrcpy(m_id, id);
    } else {
        m_id = NULL;
    }
}

void Location::SetName(const TCHAR* name)
{
    if (m_name) delete[] m_name;
    if (name) {
        m_name = new TCHAR[lstrlen(name) + 1];
        lstrcpy(m_name, name);
    } else {
        m_name = NULL;
    }
}

void Location::SetDescription(const TCHAR* description)
{
    if (m_description) delete[] m_description;
    if (description) {
        m_description = new TCHAR[lstrlen(description) + 1];
        lstrcpy(m_description, description);
    } else {
        m_description = NULL;
    }
}

void Location::SetParentId(const TCHAR* parentId)
{
    if (m_parentId) delete[] m_parentId;
    if (parentId) {
        m_parentId = new TCHAR[lstrlen(parentId) + 1];
        lstrcpy(m_parentId, parentId);
    } else {
        m_parentId = NULL;
    }
}

void Location::SetPath(const TCHAR* path)
{
    if (m_path) delete[] m_path;
    if (path) {
        m_path = new TCHAR[lstrlen(path) + 1];
        lstrcpy(m_path, path);
    } else {
        m_path = NULL;
    }
}

bool Location::FromJson(const TCHAR* json)
{
    if (!json) {
        return false;
    }

    JsonLite parser;
    if (!parser.Parse(json)) {
        return false;
    }

    // Extract fields
    AssignField(parser, TEXT("id"),          this, &Location::SetId);
    AssignField(parser, TEXT("name"),        this, &Location::SetName);
    AssignField(parser, TEXT("description"), this, &Location::SetDescription);
    AssignField(parser, TEXT("parentId"),    this, &Location::SetParentId);
    AssignField(parser, TEXT("path"),        this, &Location::SetPath);

    return IsValid();
}

TCHAR* Location::ToJson() const
{
    // Same construction as Item::ToJson: a growable buffer with escaped values,
    // since the fields are caller-sized and a location name can legitimately
    // contain a quote or a slash.
    Str::Buffer out;
    out.AppendChar((TCHAR)'{');

    bool first = true;
    AppendField(out, &first, TEXT("id"),          m_id);
    AppendField(out, &first, TEXT("name"),        m_name);
    AppendField(out, &first, TEXT("description"), m_description);
    AppendField(out, &first, TEXT("parentId"),    m_parentId);
    AppendField(out, &first, TEXT("path"),        m_path);

    out.AppendChar((TCHAR)'}');

    if (out.Failed()) {
        return NULL;
    }

    return out.Detach();
}

bool Location::IsValid() const
{
    return (m_id != NULL && lstrlen(m_id) > 0);
}

} // namespace Models
} // namespace HBX
