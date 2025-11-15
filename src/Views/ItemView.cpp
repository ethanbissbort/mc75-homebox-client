#include "../../include/Views/ItemView.hpp"

namespace HBX {
namespace Views {

ItemView::ItemView()
    : m_hwnd(NULL)
    , m_barcodeEdit(NULL)
    , m_nameEdit(NULL)
    , m_descEdit(NULL)
    , m_locationEdit(NULL)
    , m_quantityEdit(NULL)
    , m_categoryEdit(NULL)
    , m_saveButton(NULL)
    , m_cancelButton(NULL)
    , m_hInstance(NULL)
    , m_editable(false)
    , m_hasChanges(false)
    , m_saveCallback(NULL)
    , m_callbackUserData(NULL)
{
}

ItemView::~ItemView()
{
    Destroy();
}

bool ItemView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;
    // TODO: Create window and controls
    return true;
}

void ItemView::Show(bool visible)
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
    }
}

void ItemView::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

HWND ItemView::GetHandle() const
{
    return m_hwnd;
}

void ItemView::DisplayItem(const Models::Item* item)
{
    // TODO: Display item data in controls
}

bool ItemView::GetItemData(Models::Item* item) const
{
    // TODO: Get item data from controls
    return false;
}

void ItemView::Clear()
{
    // TODO: Clear all controls
}

void ItemView::SetEditable(bool editable)
{
    m_editable = editable;
    EnableControls(editable);
}

bool ItemView::IsEditable() const
{
    return m_editable;
}

bool ItemView::HasChanges() const
{
    return m_hasChanges;
}

void ItemView::SetSaveCallback(SaveCallback callback, void* userData)
{
    m_saveCallback = callback;
    m_callbackUserData = userData;
}

LRESULT CALLBACK ItemView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // TODO: Implement window procedure
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ItemView::OnSaveClick()
{
    // TODO: Handle save
}

void ItemView::OnCancelClick()
{
    // TODO: Handle cancel
}

void ItemView::OnTextChanged()
{
    m_hasChanges = true;
}

void ItemView::LayoutControls()
{
    // TODO: Layout controls
}

void ItemView::EnableControls(bool enabled)
{
    // TODO: Enable/disable edit controls
}

} // namespace Views
} // namespace HBX
