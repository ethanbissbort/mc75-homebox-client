#ifndef VIEWS_ITEMVIEW_HPP
#define VIEWS_ITEMVIEW_HPP

#include <windows.h>
#include "ViewHelpers.hpp"
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

    /**
     * Prepares the form for an item the server does not know yet: every field
     * is cleared, the id is dropped (so a save creates rather than updates) and
     * `barcode` is pre-filled from the scan that missed.
     */
    void DisplayNewItem(const TCHAR* barcode);

    bool GetItemData(Models::Item* item) const;
    void Clear();

    // UI state
    void SetEditable(bool editable);
    bool IsEditable() const;
    bool HasChanges() const;

    // Callback for save events
    typedef void (*SaveCallback)(const Models::Item* item, void* userData);
    void SetSaveCallback(SaveCallback callback, void* userData);

    // Callback raised when the user backs out of the editor
    typedef void (*CancelCallback)(void* userData);
    void SetCancelCallback(CancelCallback callback, void* userData);

private:
    enum {
        FIELD_COUNT = 6,        // barcode, name, description, location, quantity, category
        DESCRIPTION_ROW = 2,    // the one row that gets a taller edit control
        MAX_QUANTITY_DIGITS = 6,
        MAX_FIELD_CHARS = 512
    };

    HWND m_hwnd;
    HWND m_labels[FIELD_COUNT];
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

    // Id of the item currently on screen, or NULL when the form describes an
    // item that does not exist on the server yet. Without it every save looked
    // like a create and duplicated the record instead of updating it.
    TCHAR* m_itemId;

    SaveCallback m_saveCallback;
    void* m_callbackUserData;
    CancelCallback m_cancelCallback;
    void* m_cancelUserData;

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Event handlers
    void OnSaveClick();
    void OnCancelClick();
    void OnTextChanged();

    // UI layout
    void LayoutControls();
    void EnableControls(bool enabled);

    void SetItemId(const TCHAR* id);
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_ITEMVIEW_HPP
