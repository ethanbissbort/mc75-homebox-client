#include "../../include/Models/AssetSummary.hpp"
#include "../../include/StrUtil.hpp"

namespace HBX {
namespace Models {

AssetSummary::AssetSummary()
    : m_fieldCount(0)
{
    Clear();
}

void AssetSummary::Clear()
{
    m_id[0] = 0;
    m_title[0] = 0;
    m_subtitle[0] = 0;
    m_status[0] = 0;
    m_code[0] = 0;
    m_source[0] = 0;

    // Only the rows in use need clearing; the rest are unreachable while
    // m_fieldCount bounds every accessor.
    for (int i = 0; i < m_fieldCount && i < FIELD_COUNT; i++) {
        m_labels[i][0] = 0;
        m_values[i][0] = 0;
    }
    m_fieldCount = 0;
}

void AssetSummary::SetId(const TCHAR* id)             { Str::Copy(m_id, ID_MAX, id); }
const TCHAR* AssetSummary::GetId() const              { return m_id; }

void AssetSummary::SetTitle(const TCHAR* title)       { Str::Copy(m_title, VALUE_MAX, title); }
const TCHAR* AssetSummary::GetTitle() const           { return m_title; }

void AssetSummary::SetSubtitle(const TCHAR* subtitle) { Str::Copy(m_subtitle, VALUE_MAX, subtitle); }
const TCHAR* AssetSummary::GetSubtitle() const        { return m_subtitle; }

void AssetSummary::SetStatus(const TCHAR* status)     { Str::Copy(m_status, STATUS_MAX, status); }
const TCHAR* AssetSummary::GetStatus() const          { return m_status; }

void AssetSummary::SetCode(const TCHAR* code)         { Str::Copy(m_code, VALUE_MAX, code); }
const TCHAR* AssetSummary::GetCode() const            { return m_code; }

void AssetSummary::SetSource(const TCHAR* source)     { Str::Copy(m_source, LABEL_MAX, source); }
const TCHAR* AssetSummary::GetSource() const          { return m_source; }

bool AssetSummary::AddField(const TCHAR* label, const TCHAR* value)
{
    if (m_fieldCount >= FIELD_COUNT) {
        return false;
    }
    if (!label || label[0] == 0) {
        return false;
    }

    Str::Copy(m_labels[m_fieldCount], LABEL_MAX, label);
    Str::Copy(m_values[m_fieldCount], VALUE_MAX, value ? value : TEXT(""));
    m_fieldCount++;

    return true;
}

int AssetSummary::GetFieldCount() const
{
    return m_fieldCount;
}

const TCHAR* AssetSummary::GetLabel(int index) const
{
    if (index < 0 || index >= m_fieldCount) {
        return TEXT("");
    }
    return m_labels[index];
}

const TCHAR* AssetSummary::GetValue(int index) const
{
    if (index < 0 || index >= m_fieldCount) {
        return TEXT("");
    }
    return m_values[index];
}

const TCHAR* AssetSummary::FindValue(const TCHAR* label) const
{
    if (!label) {
        return NULL;
    }
    for (int i = 0; i < m_fieldCount; i++) {
        if (lstrcmp(m_labels[i], label) == 0) {
            return m_values[i];
        }
    }
    return NULL;
}

} // namespace Models
} // namespace HBX
