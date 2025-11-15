#ifndef VIEWS_ITEMVIEW_HPP
#define VIEWS_ITEMVIEW_HPP

#include <windows.h>
#include "../Models/Item.hpp"

namespace HBX {
namespace Views {

/**
 * Item display and edit interface
 * Shows item details and allows editing
 */
class ItemView {
public:
    ItemView();
    ~ItemView();

    // Window management
    bool Create(HWND parentWnd, HINSTANCE hInstance);
    void Show(bool visible);
    void Destroy();
    HWND GetHandle() const;

    // Data binding
    void DisplayItem(const Models::Item* item);
    bool GetItemData(Models::Item* item) const;
    void Clear();

    // UI state
    void SetEditable(bool editable);
    bool IsEditable() const;
    bool HasChanges() const;

    // Callback for save events
    typedef void (*SaveCallback)(const Models::Item* item, void* userData);
    void SetSaveCallback(SaveCallback callback, void* userData);

private:
    HWND m_hwnd;
    HWND m_barcodeEdit;
    HWND m_nameEdit;
    HWND m_descEdit;
    HWND m_locationEdit;
    HWND m_quantityEdit;
    HWND m_categoryEdit;
    HWND m_saveButton;
    HWND m_cancelButton;
    HINSTANCE m_hInstance;
    bool m_editable;
    bool m_hasChanges;
    SaveCallback m_saveCallback;
    void* m_callbackUserData;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Event handlers
    void OnSaveClick();
    void OnCancelClick();
    void OnTextChanged();

    // UI layout
    void LayoutControls();
    void EnableControls(bool enabled);
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_ITEMVIEW_HPP
