#include "../../include/Models/Location.hpp"
#include "../../include/Models/JsonLite.hpp"

namespace HBX {
namespace Models {

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
    TCHAR buffer[512];

    if (parser.GetString(TEXT("id"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetId(buffer);
    }

    if (parser.GetString(TEXT("name"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetName(buffer);
    }

    if (parser.GetString(TEXT("description"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetDescription(buffer);
    }

    if (parser.GetString(TEXT("parentId"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetParentId(buffer);
    }

    if (parser.GetString(TEXT("path"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetPath(buffer);
    }

    return IsValid();
}

TCHAR* Location::ToJson() const
{
    // Build JSON string manually
    TCHAR* json = new TCHAR[2048];
    int pos = 0;

    json[pos++] = '{';

    // Add id
    if (m_id) {
        wsprintf(json + pos, TEXT("\"id\":\"%s\","), m_id);
        pos = lstrlen(json);
    }

    // Add name
    if (m_name) {
        wsprintf(json + pos, TEXT("\"name\":\"%s\","), m_name);
        pos = lstrlen(json);
    }

    // Add description
    if (m_description) {
        wsprintf(json + pos, TEXT("\"description\":\"%s\","), m_description);
        pos = lstrlen(json);
    }

    // Add parentId
    if (m_parentId) {
        wsprintf(json + pos, TEXT("\"parentId\":\"%s\","), m_parentId);
        pos = lstrlen(json);
    }

    // Add path (remove trailing comma)
    if (m_path) {
        wsprintf(json + pos, TEXT("\"path\":\"%s\""), m_path);
        pos = lstrlen(json);
    } else {
        // Remove trailing comma if exists
        if (pos > 1 && json[pos - 1] == ',') {
            pos--;
        }
    }

    json[pos++] = '}';
    json[pos] = '\0';

    return json;
}

bool Location::IsValid() const
{
    return (m_id != NULL && lstrlen(m_id) > 0);
}

} // namespace Models
} // namespace HBX
