#ifndef MODELS_ASSETSUMMARY_HPP
#define MODELS_ASSETSUMMARY_HPP

#include <windows.h>

namespace HBX {
namespace Models {

/**
 * Backend-neutral, read-only view of one scanned asset.
 *
 * HomeBox items and NetBox devices have little in common beyond "something a
 * barcode resolves to": an Item is (barcode, name, description, quantity,
 * location, category), while a Device is (name, model, manufacturer, serial,
 * asset tag, site, location, rack, position, face, status, role). Forcing one
 * into the other would lose most of whichever lost, so each backend keeps its
 * own model and both project into this summary for display.
 *
 * Storage is fixed-size and heap-free. The detail screen is a 240x320 list that
 * shows at most a handful of rows, so an allocation per field would cost more
 * than it saves, and a scan-heavy workflow allocating on every decode is
 * exactly what fragments a Windows CE heap over a long shift. Values longer
 * than VALUE_MAX are truncated for display only -- the authoritative value
 * stays in the backend's own model.
 */
class AssetSummary {
public:
    enum {
        FIELD_COUNT = 12,   // rows the detail list can hold
        LABEL_MAX   = 24,   // "Manufacturer" is the longest label in use
        VALUE_MAX   = 128,  // a site/location path is the longest value
        ID_MAX      = 64,
        STATUS_MAX  = 32
    };

    AssetSummary();

    void Clear();

    // ---- identity -------------------------------------------------------

    /** Backend-native identifier used to address this asset in later calls. */
    void SetId(const TCHAR* id);
    const TCHAR* GetId() const;

    /** Primary line on the detail screen (item name / device name). */
    void SetTitle(const TCHAR* title);
    const TCHAR* GetTitle() const;

    /** Secondary line (category / manufacturer + model). */
    void SetSubtitle(const TCHAR* subtitle);
    const TCHAR* GetSubtitle() const;

    /** Human-readable status, empty when the backend has no such concept. */
    void SetStatus(const TCHAR* status);
    const TCHAR* GetStatus() const;

    /** The code that was scanned to find this asset. */
    void SetCode(const TCHAR* code);
    const TCHAR* GetCode() const;

    /** Which backend produced this summary, for the UI to label it. */
    void SetSource(const TCHAR* source);
    const TCHAR* GetSource() const;

    // ---- detail rows ----------------------------------------------------

    /**
     * Appends a label/value row. Rows past FIELD_COUNT are dropped rather than
     * overwriting earlier ones, so the most important fields -- which every
     * producer adds first -- always survive. Returns false when full.
     */
    bool AddField(const TCHAR* label, const TCHAR* value);

    int GetFieldCount() const;
    const TCHAR* GetLabel(int index) const;
    const TCHAR* GetValue(int index) const;

    /** Value of the first row with this label, or NULL. */
    const TCHAR* FindValue(const TCHAR* label) const;

private:
    TCHAR m_id[ID_MAX];
    TCHAR m_title[VALUE_MAX];
    TCHAR m_subtitle[VALUE_MAX];
    TCHAR m_status[STATUS_MAX];
    TCHAR m_code[VALUE_MAX];
    TCHAR m_source[LABEL_MAX];

    TCHAR m_labels[FIELD_COUNT][LABEL_MAX];
    TCHAR m_values[FIELD_COUNT][VALUE_MAX];
    int m_fieldCount;
};

} // namespace Models
} // namespace HBX

#endif // MODELS_ASSETSUMMARY_HPP
