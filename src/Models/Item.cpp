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

    if (parser.GetString(TEXT("barcode"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetBarcode(buffer);
    }

    if (parser.GetString(TEXT("name"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetName(buffer);
    }

    if (parser.GetString(TEXT("description"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetDescription(buffer);
    }

    if (parser.GetString(TEXT("locationId"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetLocationId(buffer);
    }

    if (parser.GetString(TEXT("category"), buffer, sizeof(buffer) / sizeof(TCHAR))) {
        SetCategory(buffer);
    }

    int quantity = 0;
    if (parser.GetInt(TEXT("quantity"), &quantity)) {
        SetQuantity(quantity);
    }

    return IsValid();
}

TCHAR* Item::ToJson() const
{
    // Build JSON string manually
    TCHAR* json = new TCHAR[2048];
    int pos = 0;

    // Helper lambda for adding strings (manual implementation for C++03)
    json[pos++] = '{';

    // Add id
    if (m_id) {
        wsprintf(json + pos, TEXT("\"id\":\"%s\","), m_id);
        pos = lstrlen(json);
    }

    // Add barcode
    if (m_barcode) {
        wsprintf(json + pos, TEXT("\"barcode\":\"%s\","), m_barcode);
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

    // Add locationId
    if (m_locationId) {
        wsprintf(json + pos, TEXT("\"locationId\":\"%s\","), m_locationId);
        pos = lstrlen(json);
    }

    // Add category
    if (m_category) {
        wsprintf(json + pos, TEXT("\"category\":\"%s\","), m_category);
        pos = lstrlen(json);
    }

    // Add quantity
    wsprintf(json + pos, TEXT("\"quantity\":%d"), m_quantity);
    pos = lstrlen(json);

    json[pos++] = '}';
    json[pos] = '\0';

    return json;
}

bool Item::IsValid() const
{
    return (m_barcode != NULL && lstrlen(m_barcode) > 0);
}

} // namespace Models
} // namespace HBX
