#include "../../include/Models/Location.hpp"

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
    // TODO: Parse JSON
    return false;
}

TCHAR* Location::ToJson() const
{
    // TODO: Generate JSON
    return NULL;
}

bool Location::IsValid() const
{
    return (m_id != NULL && lstrlen(m_id) > 0);
}

} // namespace Models
} // namespace HBX
