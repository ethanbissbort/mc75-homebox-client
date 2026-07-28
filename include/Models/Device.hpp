#ifndef MODELS_DEVICE_HPP
#define MODELS_DEVICE_HPP

#include <windows.h>

namespace HBX {
namespace Models {

class JsonLite;

/**
 * One NetBox DCIM device.
 *
 * This is the NetBox counterpart of Models::Item, and the two deliberately do
 * not share a representation: an Item is a countable thing in a box, a Device
 * is a single unit bolted into a rack. Both project into Models::AssetSummary
 * for the screen; only this class knows the NetBox field names.
 *
 * STORAGE -- fixed buffers, no heap.
 *
 * Item allocates one string per field, which is right for Item: its description
 * is free text with no server-side limit. A Device is the opposite case. Every
 * field here maps to a NetBox column with a declared max_length (names are 100,
 * serial and asset_tag are 50, status values are short choice tokens), so the
 * worst case is known at compile time, and a device is parsed on the scan hot
 * path -- once per trigger pull, plus once per row of a disambiguation list.
 * Fifteen allocations and fifteen frees per scan, several hundred times a
 * shift, is exactly the traffic that fragments a Windows CE heap; one ~2 KB
 * object that is filled in place costs nothing per scan and cannot leak.
 *
 * The trade is that an over-long field is truncated rather than kept whole.
 * That is acceptable here and not in Item, because a truncated Device field is
 * only ever displayed: writes address the device by GetId() and send the keys
 * the operator changed, so nothing read here is ever written back.
 *
 * There is no ToJson(). A device is never sent back wholesale -- see
 * NbClient::MoveDevice and SetDeviceStatus, which PATCH the two or three keys
 * that changed. Serialising the whole record would blank every field the
 * handheld never loaded.
 */
class Device {
public:
    enum {
        /** NetBox primary keys are integers; rendered as text for URLs and queue payloads. */
        ID_MAX       = 24,

        /** Site / Location / Rack / DeviceType / Manufacturer / Role names are max_length=100. */
        NAME_MAX     = 104,

        /** serial and asset_tag are both max_length=50. */
        CODE_MAX     = 56,

        /** "decommissioning" is the longest value DeviceStatusChoices ships. */
        STATUS_MAX   = 32,

        /** Its label, with room for a deployment-defined choice via FIELD_CHOICES. */
        LABEL_MAX    = 48,

        /** "front" / "rear", or empty for an unracked device. */
        FACE_MAX     = 16,

        /** An IPv6 address with a prefix length, e.g. "2001:db8:1234:5678::1/128". */
        ADDRESS_MAX  = 64,

        /** Rendered position: max_digits=4 with one decimal place, so "-999.9" fits. */
        POSITION_MAX = 12
    };

    Device();

    /** Resets every field, including the position flag. */
    void Clear();

    // ---- identity --------------------------------------------------------

    const TCHAR* GetId() const;
    void SetId(const TCHAR* id);

    /** May legitimately be empty: NetBox allows an unnamed device. */
    const TCHAR* GetName() const;
    void SetName(const TCHAR* name);

    const TCHAR* GetSerial() const;
    void SetSerial(const TCHAR* serial);

    const TCHAR* GetAssetTag() const;
    void SetAssetTag(const TCHAR* assetTag);

    // ---- hardware --------------------------------------------------------

    const TCHAR* GetDeviceTypeModel() const;
    void SetDeviceTypeModel(const TCHAR* model);

    const TCHAR* GetManufacturer() const;
    void SetManufacturer(const TCHAR* manufacturer);

    /** From `role`, or `device_role` on NetBox 3.x. See FromJson. */
    const TCHAR* GetDeviceRole() const;
    void SetDeviceRole(const TCHAR* role);

    const TCHAR* GetPrimaryIp() const;
    void SetPrimaryIp(const TCHAR* address);

    // ---- placement -------------------------------------------------------
    //
    // Each level carries both the display name and the numeric id: the name is
    // what the operator reads, the id is what a PATCH sends. A read returns
    // only the nested object, so both have to be pulled out at parse time or a
    // later move would need a second round trip just to re-learn the ids.

    const TCHAR* GetSiteName() const;
    void SetSiteName(const TCHAR* name);
    const TCHAR* GetSiteId() const;
    void SetSiteId(const TCHAR* id);

    const TCHAR* GetLocationName() const;
    void SetLocationName(const TCHAR* name);
    const TCHAR* GetLocationId() const;
    void SetLocationId(const TCHAR* id);

    const TCHAR* GetRackName() const;
    void SetRackName(const TCHAR* name);
    const TCHAR* GetRackId() const;
    void SetRackId(const TCHAR* id);

    /**
     * Rack unit. NetBox declares this DecimalField(max_digits=4,
     * decimal_places=1) because racks support half-U mounting, so 42.5 is a
     * real position and reading it as an integer silently drops the half.
     *
     * The separate flag is not redundant with a sentinel: 0 would be a legal
     * value for a decimal field, while `null` -- what an unracked device
     * carries -- is not a number at all.
     */
    bool HasPosition() const;
    double GetPosition() const;
    void SetPosition(double position);
    void ClearPosition();

    /** Position as text ("12", "42.5"), or an empty string when unracked. */
    bool GetPositionText(TCHAR* out, int cap) const;

    /**
     * Renders one NetBox position. Exactly one decimal place is both necessary
     * and sufficient (decimal_places=1), and a trailing ".0" is dropped so a
     * whole unit reads as "12" rather than "12.0" on a 240px row.
     */
    static bool FormatPosition(double position, TCHAR* out, int cap);

    /** "front" / "rear", empty when the device is not face-mounted. */
    const TCHAR* GetFace() const;
    void SetFace(const TCHAR* face);

    // ---- status ----------------------------------------------------------

    /** Machine value, e.g. "active" -- this is what a PATCH sends. */
    const TCHAR* GetStatus() const;
    void SetStatus(const TCHAR* status);

    /** Human label, e.g. "Active" -- this is what the screen shows. */
    const TCHAR* GetStatusLabel() const;
    void SetStatusLabel(const TCHAR* label);

    // ---- serialization ---------------------------------------------------

    /**
     * Parses one NetBox device object. `json` must be the device itself, not
     * the {"count","next","results":[...]} envelope a list endpoint returns --
     * NbClient unwraps that and calls the JsonLite overload per element.
     *
     * Every field is optional. NetBox nulls `name`, `tenant`, `platform`,
     * `location`, `rack`, `position` and `primary_ip` routinely, and an
     * unracked device nulls three of them at once, so a missing field leaves
     * its buffer empty rather than failing the parse. Returns true once an id
     * was found, since that is the only field a later write cannot do without.
     */
    bool FromJson(const TCHAR* json);

    /**
     * As above, reading from an already-parsed node. This is the form NbClient
     * uses for each element of a `results` array: re-serialising a borrowed
     * node back to text only to parse it again would double the per-scan cost
     * of a disambiguation list for nothing.
     */
    bool FromJson(const JsonLite& node);

    /** True once the device carries an id; every write addresses it by id. */
    bool IsValid() const;

private:
    TCHAR m_id[ID_MAX];
    TCHAR m_name[NAME_MAX];
    TCHAR m_deviceTypeModel[NAME_MAX];
    TCHAR m_manufacturer[NAME_MAX];
    TCHAR m_deviceRole[NAME_MAX];
    TCHAR m_serial[CODE_MAX];
    TCHAR m_assetTag[CODE_MAX];

    TCHAR m_siteName[NAME_MAX];
    TCHAR m_siteId[ID_MAX];
    TCHAR m_locationName[NAME_MAX];
    TCHAR m_locationId[ID_MAX];
    TCHAR m_rackName[NAME_MAX];
    TCHAR m_rackId[ID_MAX];

    double m_position;
    bool m_hasPosition;
    TCHAR m_face[FACE_MAX];

    TCHAR m_status[STATUS_MAX];
    TCHAR m_statusLabel[LABEL_MAX];
    TCHAR m_primaryIp[ADDRESS_MAX];
};

} // namespace Models
} // namespace HBX

#endif // MODELS_DEVICE_HPP
