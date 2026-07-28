#include "../../include/Models/Device.hpp"
#include "../../include/Models/JsonLite.hpp"
#include "../../include/StrUtil.hpp"

namespace HBX {
namespace Models {

namespace {

/**
 * Reads `key` as text whatever JSON type carries it.
 *
 * NetBox renders every object id as a JSON number, but the handheld needs ids
 * as text: they are concatenated into request paths, written into queue
 * payloads and compared as strings. Accepting a quoted id as well costs one
 * branch and means a hand-edited queue payload -- or a proxy that stringifies
 * ids -- still parses.
 */
bool ReadNumberOrString(const JsonLite& src, const TCHAR* key, TCHAR* dst, int cap)
{
    if (src.GetString(key, dst, (DWORD)cap)) {
        return true;
    }

    int value = 0;
    if (!src.GetInt(key, &value)) {
        return false;
    }

    dst[0] = 0;
    return Str::AppendInt(dst, cap, (long)value);
}

/**
 * Reads the display name and the numeric id of one nested foreign key.
 *
 * Both halves are needed at parse time. A read only ever returns the nested
 * object, while a PATCH addresses the same relation by bare id, so a client
 * that kept only the name would have to re-resolve it over the radio link
 * before it could move anything.
 *
 * Leaves both outputs untouched when the key is absent or null -- which is
 * exactly what `location` and `rack` carry for an unracked device.
 */
void ReadRelated(const JsonLite& src, const TCHAR* key,
                 TCHAR* nameOut, int nameCap, TCHAR* idOut, int idCap)
{
    JsonLite related;
    if (!src.GetObject(key, &related)) {
        return;
    }

    related.GetString(TEXT("name"), nameOut, (DWORD)nameCap);
    ReadNumberOrString(related, TEXT("id"), idOut, idCap);
}

/**
 * Reads a NetBox ChoiceField, which serializes as {"value","label"}.
 *
 * The bare-string fallback is not idle tolerance. `status` and `face` come back
 * as objects but are *written* as bare strings, so the same key has two shapes
 * on the wire; accepting both here means a payload echoed from a write path, or
 * from a deployment whose proxy flattens choice fields, still reads correctly.
 */
void ReadChoice(const JsonLite& src, const TCHAR* key,
                TCHAR* valueOut, int valueCap, TCHAR* labelOut, int labelCap)
{
    JsonLite choice;
    if (src.GetObject(key, &choice)) {
        choice.GetString(TEXT("value"), valueOut, (DWORD)valueCap);
        if (labelOut && labelCap > 0) {
            choice.GetString(TEXT("label"), labelOut, (DWORD)labelCap);
        }
        return;
    }

    src.GetString(key, valueOut, (DWORD)valueCap);
}

} // namespace

Device::Device()
    : m_position(0.0)
    , m_hasPosition(false)
{
    Clear();
}

void Device::Clear()
{
    m_id[0] = 0;
    m_name[0] = 0;
    m_deviceTypeModel[0] = 0;
    m_manufacturer[0] = 0;
    m_deviceRole[0] = 0;
    m_serial[0] = 0;
    m_assetTag[0] = 0;

    m_siteName[0] = 0;
    m_siteId[0] = 0;
    m_locationName[0] = 0;
    m_locationId[0] = 0;
    m_rackName[0] = 0;
    m_rackId[0] = 0;

    m_position = 0.0;
    m_hasPosition = false;
    m_face[0] = 0;

    m_status[0] = 0;
    m_statusLabel[0] = 0;
    m_primaryIp[0] = 0;
}

// ---- identity -------------------------------------------------------------

const TCHAR* Device::GetId() const                  { return m_id; }
void Device::SetId(const TCHAR* id)                 { Str::Copy(m_id, ID_MAX, id); }

const TCHAR* Device::GetName() const                { return m_name; }
void Device::SetName(const TCHAR* name)             { Str::Copy(m_name, NAME_MAX, name); }

const TCHAR* Device::GetSerial() const              { return m_serial; }
void Device::SetSerial(const TCHAR* serial)         { Str::Copy(m_serial, CODE_MAX, serial); }

const TCHAR* Device::GetAssetTag() const            { return m_assetTag; }
void Device::SetAssetTag(const TCHAR* assetTag)     { Str::Copy(m_assetTag, CODE_MAX, assetTag); }

// ---- hardware -------------------------------------------------------------

const TCHAR* Device::GetDeviceTypeModel() const     { return m_deviceTypeModel; }
void Device::SetDeviceTypeModel(const TCHAR* model) { Str::Copy(m_deviceTypeModel, NAME_MAX, model); }

const TCHAR* Device::GetManufacturer() const        { return m_manufacturer; }
void Device::SetManufacturer(const TCHAR* v)        { Str::Copy(m_manufacturer, NAME_MAX, v); }

const TCHAR* Device::GetDeviceRole() const          { return m_deviceRole; }
void Device::SetDeviceRole(const TCHAR* role)       { Str::Copy(m_deviceRole, NAME_MAX, role); }

const TCHAR* Device::GetPrimaryIp() const           { return m_primaryIp; }
void Device::SetPrimaryIp(const TCHAR* address)     { Str::Copy(m_primaryIp, ADDRESS_MAX, address); }

// ---- placement ------------------------------------------------------------

const TCHAR* Device::GetSiteName() const            { return m_siteName; }
void Device::SetSiteName(const TCHAR* name)         { Str::Copy(m_siteName, NAME_MAX, name); }
const TCHAR* Device::GetSiteId() const              { return m_siteId; }
void Device::SetSiteId(const TCHAR* id)             { Str::Copy(m_siteId, ID_MAX, id); }

const TCHAR* Device::GetLocationName() const        { return m_locationName; }
void Device::SetLocationName(const TCHAR* name)     { Str::Copy(m_locationName, NAME_MAX, name); }
const TCHAR* Device::GetLocationId() const          { return m_locationId; }
void Device::SetLocationId(const TCHAR* id)         { Str::Copy(m_locationId, ID_MAX, id); }

const TCHAR* Device::GetRackName() const            { return m_rackName; }
void Device::SetRackName(const TCHAR* name)         { Str::Copy(m_rackName, NAME_MAX, name); }
const TCHAR* Device::GetRackId() const              { return m_rackId; }
void Device::SetRackId(const TCHAR* id)             { Str::Copy(m_rackId, ID_MAX, id); }

bool Device::HasPosition() const                    { return m_hasPosition; }
double Device::GetPosition() const                  { return m_position; }

void Device::SetPosition(double position)
{
    m_position = position;
    m_hasPosition = true;
}

void Device::ClearPosition()
{
    m_position = 0.0;
    m_hasPosition = false;
}

bool Device::GetPositionText(TCHAR* out, int cap) const
{
    if (!out || cap <= 0) {
        return false;
    }

    out[0] = 0;
    if (!m_hasPosition) {
        // An unracked device has no position at all. Rendering the zero the
        // member happens to hold would put a device in U0 on the screen.
        return true;
    }

    return FormatPosition(m_position, out, cap);
}

bool Device::FormatPosition(double position, TCHAR* out, int cap)
{
    if (!out || cap <= 0) {
        return false;
    }

    out[0] = 0;

    bool negative = (position < 0.0);
    double magnitude = negative ? -position : position;

    // NetBox declares position as max_digits=4, so anything past four digits is
    // not a rack unit. Refusing it keeps the conversion below inside the range
    // of a long on a 32-bit target, where overflowing the cast is undefined
    // rather than merely wrong.
    if (magnitude >= 100000.0) {
        return false;
    }

    // Round once, then split. Rounding after the split would let a value that
    // decoded as 42.4999 render as "42.4" here and as "42.5" wherever the
    // server shows it.
    long tenths = (long)(magnitude * 10.0 + 0.5);
    long whole = tenths / 10;
    long frac = tenths % 10;

    bool ok = true;
    if (negative && tenths != 0) {
        ok = Str::AppendChar(out, cap, (TCHAR)'-') && ok;
    }
    ok = Str::AppendInt(out, cap, whole) && ok;

    // A whole unit reads as "12", not "12.0": the decimal only appears for the
    // half-U slots that actually need it, which matters on a 240px row.
    if (frac != 0) {
        ok = Str::AppendChar(out, cap, (TCHAR)'.') && ok;
        ok = Str::AppendInt(out, cap, frac) && ok;
    }

    return ok;
}

const TCHAR* Device::GetFace() const                { return m_face; }
void Device::SetFace(const TCHAR* face)             { Str::Copy(m_face, FACE_MAX, face); }

// ---- status ---------------------------------------------------------------

const TCHAR* Device::GetStatus() const              { return m_status; }
void Device::SetStatus(const TCHAR* status)         { Str::Copy(m_status, STATUS_MAX, status); }

const TCHAR* Device::GetStatusLabel() const         { return m_statusLabel; }
void Device::SetStatusLabel(const TCHAR* label)     { Str::Copy(m_statusLabel, LABEL_MAX, label); }

// ---- serialization --------------------------------------------------------

bool Device::FromJson(const TCHAR* json)
{
    if (!json) {
        Clear();
        return false;
    }

    JsonLite parser;
    if (!parser.Parse(json)) {
        Clear();
        return false;
    }

    return FromJson(parser);
}

bool Device::FromJson(const JsonLite& node)
{
    // Cleared first so every field is either read from this document or left
    // empty. Without it a sparse device would show the previous scan's site.
    Clear();

    ReadNumberOrString(node, TEXT("id"), m_id, ID_MAX);

    // `name` is nullable in NetBox -- an unnamed device is legal, and common
    // for gear that has only just been received.
    node.GetString(TEXT("name"), m_name, (DWORD)NAME_MAX);
    node.GetString(TEXT("serial"), m_serial, (DWORD)CODE_MAX);
    node.GetString(TEXT("asset_tag"), m_assetTag, (DWORD)CODE_MAX);

    // The manufacturer sits at device_type.manufacturer.name -- three levels
    // down -- so the descent is chained rather than a single nested read.
    JsonLite deviceType;
    if (node.GetObject(TEXT("device_type"), &deviceType)) {
        deviceType.GetString(TEXT("model"), m_deviceTypeModel, (DWORD)NAME_MAX);
        deviceType.GetNestedString(TEXT("manufacturer"), TEXT("name"), m_manufacturer, NAME_MAX);
    }

    // `device_role` was renamed `role` in NetBox 4.0; 3.6 and 3.7 carry both,
    // with `role` authoritative. Preferring `role` and falling back covers 3.6
    // through 4.6 with one code path, so nothing here branches on the server
    // version -- which is why a version that could not be probed is harmless.
    JsonLite role;
    if (node.GetObject(TEXT("role"), &role) ||
        node.GetObject(TEXT("device_role"), &role)) {
        role.GetString(TEXT("name"), m_deviceRole, (DWORD)NAME_MAX);
    }

    ReadRelated(node, TEXT("site"),     m_siteName,     NAME_MAX, m_siteId,     ID_MAX);
    ReadRelated(node, TEXT("location"), m_locationName, NAME_MAX, m_locationId, ID_MAX);
    ReadRelated(node, TEXT("rack"),     m_rackName,     NAME_MAX, m_rackId,     ID_MAX);

    // Decimal, never integer: racks take half-U slots, and reading 42.5 with
    // GetInt truncates to 42 without reporting anything. A null position (an
    // unracked device) simply leaves the flag clear.
    double position = 0.0;
    if (node.GetDouble(TEXT("position"), &position)) {
        SetPosition(position);
    }

    ReadChoice(node, TEXT("face"), m_face, FACE_MAX, NULL, 0);
    ReadChoice(node, TEXT("status"), m_status, STATUS_MAX, m_statusLabel, LABEL_MAX);

    // primary_ip is whichever of primary_ip4 / primary_ip6 the device prefers,
    // and is null when neither is assigned. `display` is the fallback because
    // the 3.x nested serializer is the only shape that might omit `address`.
    if (!node.GetNestedString(TEXT("primary_ip"), TEXT("address"), m_primaryIp, ADDRESS_MAX)) {
        node.GetNestedString(TEXT("primary_ip"), TEXT("display"), m_primaryIp, ADDRESS_MAX);
    }

    return IsValid();
}

bool Device::IsValid() const
{
    // The id is the one field nothing works without: every status change and
    // every move PATCHes /api/dcim/devices/<id>/. A device parsed without one
    // could be displayed but never acted on, which is worse than a clean miss.
    return m_id[0] != 0;
}

} // namespace Models
} // namespace HBX
