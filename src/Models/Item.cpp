#include "../../include/Models/Item.hpp"
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

// Reads one string field at its full length. Extracting through a shared fixed
// buffer would cap every field, and the queue round-trip (GetItem, offline
// edit, UpdateItem) would then write the shortened text back over the server's
// full value.
void AssignField(const JsonLite& parser, const TCHAR* key, Item* item,
                 void (Item::*setter)(const TCHAR*))
{
    TCHAR* value = parser.GetStringAlloc(key);
    if (!value) {
        return;
    }
    (item->*setter)(value);
    delete[] value;
}

} // namespace

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
    AssignField(parser, TEXT("id"),          this, &Item::SetId);
    AssignField(parser, TEXT("barcode"),     this, &Item::SetBarcode);
    AssignField(parser, TEXT("name"),        this, &Item::SetName);
    AssignField(parser, TEXT("description"), this, &Item::SetDescription);
    AssignField(parser, TEXT("locationId"),  this, &Item::SetLocationId);
    AssignField(parser, TEXT("category"),    this, &Item::SetCategory);

    int quantity = 0;
    if (parser.GetInt(TEXT("quantity"), &quantity)) {
        SetQuantity(quantity);
    }

    return IsValid();
}

TCHAR* Item::ToJson() const
{
    // Fields are caller-sized (a scanned description has no length limit) and
    // may contain quotes, backslashes or newlines, so the document is built in
    // a growable buffer with every value escaped. Escaping the newline matters
    // beyond JSON validity: the journal queue is line-oriented, so a raw
    // newline in a payload would split one queued transaction into two
    // unreplayable lines.
    Str::Buffer out;
    out.AppendChar((TCHAR)'{');

    bool first = true;
    AppendField(out, &first, TEXT("id"),          m_id);
    AppendField(out, &first, TEXT("barcode"),     m_barcode);
    AppendField(out, &first, TEXT("name"),        m_name);
    AppendField(out, &first, TEXT("description"), m_description);
    AppendField(out, &first, TEXT("locationId"),  m_locationId);
    AppendField(out, &first, TEXT("category"),    m_category);

    // Quantity is always present, so it closes the object.
    if (!first) {
        out.AppendChar((TCHAR)',');
    }
    out.AppendJsonInt(TEXT("quantity"), m_quantity);
    out.AppendChar((TCHAR)'}');

    if (out.Failed()) {
        return NULL;
    }

    return out.Detach();
}

bool Item::IsValid() const
{
    return (m_barcode != NULL && lstrlen(m_barcode) > 0);
}

} // namespace Models
} // namespace HBX
