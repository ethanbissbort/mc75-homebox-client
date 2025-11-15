#include "../../include/Models/Item.hpp"
#include "../../include/Models/JsonLite.hpp"

namespace HBX {
namespace Models {

Item::Item()
    : m_id(NULL)
    , m_barcode(NULL)
    , m_name(NULL)
    , m_description(NULL)
    , m_locationId(NULL)
    , m_quantity(0)
    , m_category(NULL)
{
}

Item::~Item()
{
    Cleanup();
}

void Item::Cleanup()
{
    if (m_id) delete[] m_id;
    if (m_barcode) delete[] m_barcode;
    if (m_name) delete[] m_name;
    if (m_description) delete[] m_description;
    if (m_locationId) delete[] m_locationId;
    if (m_category) delete[] m_category;
    
    m_id = m_barcode = m_name = m_description = m_locationId = m_category = NULL;
}

const TCHAR* Item::GetId() const { return m_id; }
const TCHAR* Item::GetBarcode() const { return m_barcode; }
const TCHAR* Item::GetName() const { return m_name; }
const TCHAR* Item::GetDescription() const { return m_description; }
const TCHAR* Item::GetLocationId() const { return m_locationId; }
int Item::GetQuantity() const { return m_quantity; }
const TCHAR* Item::GetCategory() const { return m_category; }

void Item::SetId(const TCHAR* id)
{
    if (m_id) delete[] m_id;
    if (id) {
        m_id = new TCHAR[lstrlen(id) + 1];
        lstrcpy(m_id, id);
    } else {
        m_id = NULL;
    }
}

void Item::SetBarcode(const TCHAR* barcode)
{
    if (m_barcode) delete[] m_barcode;
    if (barcode) {
        m_barcode = new TCHAR[lstrlen(barcode) + 1];
        lstrcpy(m_barcode, barcode);
    } else {
        m_barcode = NULL;
    }
}

void Item::SetName(const TCHAR* name)
{
    if (m_name) delete[] m_name;
    if (name) {
        m_name = new TCHAR[lstrlen(name) + 1];
        lstrcpy(m_name, name);
    } else {
        m_name = NULL;
    }
}

void Item::SetDescription(const TCHAR* description)
{
    if (m_description) delete[] m_description;
    if (description) {
        m_description = new TCHAR[lstrlen(description) + 1];
        lstrcpy(m_description, description);
    } else {
        m_description = NULL;
    }
}

void Item::SetLocationId(const TCHAR* locationId)
{
    if (m_locationId) delete[] m_locationId;
    if (locationId) {
        m_locationId = new TCHAR[lstrlen(locationId) + 1];
        lstrcpy(m_locationId, locationId);
    } else {
        m_locationId = NULL;
    }
}

void Item::SetQuantity(int quantity) { m_quantity = quantity; }

void Item::SetCategory(const TCHAR* category)
{
    if (m_category) delete[] m_category;
    if (category) {
        m_category = new TCHAR[lstrlen(category) + 1];
        lstrcpy(m_category, category);
    } else {
        m_category = NULL;
    }
}

bool Item::FromJson(const TCHAR* json)
{
    // TODO: Parse JSON
    return false;
}

TCHAR* Item::ToJson() const
{
    // TODO: Generate JSON
    return NULL;
}

bool Item::IsValid() const
{
    return (m_barcode != NULL && lstrlen(m_barcode) > 0);
}

} // namespace Models
} // namespace HBX
