#include "../../include/Views/ItemView.hpp"
#include "../../include/Views/ViewHelpers.hpp"
#include "../../include/StrUtil.hpp"

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
    , m_itemId(NULL)
    , m_saveCallback(NULL)
    , m_callbackUserData(NULL)
    , m_cancelCallback(NULL)
    , m_cancelUserData(NULL)
{
    for (int i = 0; i < FIELD_COUNT; i++) {
        m_labels[i] = NULL;
    }
}

ItemView::~ItemView()
{
    Destroy();
    SetItemId(NULL);
}

bool ItemView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Register a window class that installs ItemView::WindowProc so Save/Cancel
    // clicks and edit-change notifications reach this instance (the built-in
    // STATIC class would drop them).
    static const TCHAR* kClassName = TEXT("HBXItemView");
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = ItemView::WindowProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClass(&wc); // harmless if already registered by a prior Create()

    // Create main window using our class; pass 'this' so WM_CREATE can stash
    // the instance pointer (see WindowProc).
    m_hwnd = CreateWindow(
        kClassName,
        TEXT("Item View"),
        WS_CHILD | WS_VISIBLE,
        0, 0, 240, 320,
        parentWnd,
        NULL,
        hInstance,
        (LPVOID)this
    );

    if (!m_hwnd) {
        return false;
    }

    // Controls are created at nominal positions and then placed by
    // LayoutControls, which is also what runs on every WM_SIZE.
    static const TCHAR* const kLabels[FIELD_COUNT] = {
        TEXT("Barcode:"), TEXT("Name:"), TEXT("Description:"),
        TEXT("Location:"), TEXT("Quantity:"), TEXT("Category:")
    };

    for (int i = 0; i < FIELD_COUNT; i++) {
        m_labels[i] = CreateWindow(TEXT("STATIC"), kLabels[i],
            WS_CHILD | WS_VISIBLE, 10, 10, 80, 20, m_hwnd, NULL, hInstance, NULL);
    }

    m_barcodeEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, 10, 130, 25, m_hwnd, (HMENU)2001, hInstance, NULL);

    m_nameEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, 10, 130, 25, m_hwnd, (HMENU)2002, hInstance, NULL);

    m_descEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        100, 10, 130, 50, m_hwnd, (HMENU)2003, hInstance, NULL);

    m_locationEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, 10, 130, 25, m_hwnd, (HMENU)2004, hInstance, NULL);

    m_quantityEdit = CreateWindow(TEXT("EDIT"), TEXT("0"),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        100, 10, 130, 25, m_hwnd, (HMENU)2005, hInstance, NULL);

    m_categoryEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, 10, 130, 25, m_hwnd, (HMENU)2006, hInstance, NULL);

    // ES_NUMBER only restricts the characters, not how many of them: cap the
    // digit count so a leaned-on keypad cannot push the parse past INT_MAX.
    if (m_quantityEdit) {
        SendMessage(m_quantityEdit, EM_LIMITTEXT, (WPARAM)MAX_QUANTITY_DIGITS, 0);
    }

    m_saveButton = CreateWindow(TEXT("BUTTON"), TEXT("Save"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        30, 250, 80, 35, m_hwnd, (HMENU)2007, hInstance, NULL);

    m_cancelButton = CreateWindow(TEXT("BUTTON"), TEXT("Cancel"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, 250, 80, 35, m_hwnd, (HMENU)2008, hInstance, NULL);

    LayoutControls();

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

void ItemView::SetItemId(const TCHAR* id)
{
    if (m_itemId) {
        delete[] m_itemId;
        m_itemId = NULL;
    }
    if (id && lstrlen(id) > 0) {
        m_itemId = Str::Dup(id);
    }
}

void ItemView::DisplayItem(const Models::Item* item)
{
    if (!item) {
        Clear();
        return;
    }

    // The id has no field of its own on this cramped screen, but it decides
    // whether a save updates or creates, so keep it alongside the form.
    SetItemId(item->GetId());

    if (m_barcodeEdit) {
        SetWindowText(m_barcodeEdit, item->GetBarcode() ? item->GetBarcode() : TEXT(""));
    }

    if (m_nameEdit) {
        SetWindowText(m_nameEdit, item->GetName() ? item->GetName() : TEXT(""));
    }

    if (m_descEdit) {
        SetWindowText(m_descEdit, item->GetDescription() ? item->GetDescription() : TEXT(""));
    }

    if (m_locationEdit) {
        SetWindowText(m_locationEdit, item->GetLocationId() ? item->GetLocationId() : TEXT(""));
    }

    if (m_quantityEdit) {
        TCHAR buffer[32];
        buffer[0] = 0;
        Str::AppendInt(buffer, (int)(sizeof(buffer) / sizeof(TCHAR)), item->GetQuantity());
        SetWindowText(m_quantityEdit, buffer);
    }

    if (m_categoryEdit) {
        SetWindowText(m_categoryEdit, item->GetCategory() ? item->GetCategory() : TEXT(""));
    }

    m_hasChanges = false;
}

void ItemView::DisplayNewItem(const TCHAR* barcode)
{
    Clear();
    if (m_barcodeEdit && barcode) {
        SetWindowText(m_barcodeEdit, barcode);
    }
    m_hasChanges = false;
}

bool ItemView::GetItemData(Models::Item* item) const
{
    if (!item) {
        return false;
    }

    TCHAR buffer[MAX_FIELD_CHARS];

    // Carry the displayed record's id back out, so an edit of an existing item
    // is pushed as an update instead of creating a second copy of it.
    if (m_itemId) {
        item->SetId(m_itemId);
    }

    if (m_barcodeEdit) {
        GetWindowText(m_barcodeEdit, buffer, MAX_FIELD_CHARS);
        item->SetBarcode(buffer);
    }

    if (m_nameEdit) {
        GetWindowText(m_nameEdit, buffer, MAX_FIELD_CHARS);
        item->SetName(buffer);
    }

    if (m_descEdit) {
        GetWindowText(m_descEdit, buffer, MAX_FIELD_CHARS);
        item->SetDescription(buffer);
    }

    if (m_locationEdit) {
        GetWindowText(m_locationEdit, buffer, MAX_FIELD_CHARS);
        item->SetLocationId(buffer);
    }

    if (m_quantityEdit) {
        GetWindowText(m_quantityEdit, buffer, MAX_FIELD_CHARS);
        int quantity = 0;
        if (lstrlen(buffer) > 0 && !Str::ParseInt(buffer, &quantity)) {
            return false;   // not a number at all: reject rather than save 0
        }
        if (quantity < 0) {
            quantity = 0;
        }
        item->SetQuantity(quantity);
    }

    if (m_categoryEdit) {
        GetWindowText(m_categoryEdit, buffer, MAX_FIELD_CHARS);
        item->SetCategory(buffer);
    }

    return item->IsValid();
}

void ItemView::Clear()
{
    SetItemId(NULL);

    if (m_barcodeEdit) SetWindowText(m_barcodeEdit, TEXT(""));
    if (m_nameEdit) SetWindowText(m_nameEdit, TEXT(""));
    if (m_descEdit) SetWindowText(m_descEdit, TEXT(""));
    if (m_locationEdit) SetWindowText(m_locationEdit, TEXT(""));
    if (m_quantityEdit) SetWindowText(m_quantityEdit, TEXT("0"));
    if (m_categoryEdit) SetWindowText(m_categoryEdit, TEXT(""));

    m_hasChanges = false;
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

void ItemView::SetCancelCallback(CancelCallback callback, void* userData)
{
    m_cancelCallback = callback;
    m_cancelUserData = userData;
}

LRESULT CALLBACK ItemView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    ItemView* pThis = NULL;

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (ItemView*)pCreate->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)pThis);
    } else {
        pThis = (ItemView*)GetWindowLong(hwnd, GWL_USERDATA);
    }

    switch (uMsg) {
        case WM_COMMAND:
            if (pThis) {
                WORD ctrlId = LOWORD(wParam);
                WORD notifyCode = HIWORD(wParam);

                if (ctrlId == 2007 && notifyCode == BN_CLICKED) {
                    pThis->OnSaveClick();
                    return 0;
                } else if (ctrlId == 2008 && notifyCode == BN_CLICKED) {
                    pThis->OnCancelClick();
                    return 0;
                } else if (notifyCode == EN_CHANGE) {
                    pThis->OnTextChanged();
                }
            }
            break;

        case WM_SIZE:
            if (pThis) {
                pThis->LayoutControls();
            }
            return 0;

        case WM_DESTROY:
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ItemView::OnSaveClick()
{
    if (!m_saveCallback) {
        return;
    }

    Models::Item item;
    if (GetItemData(&item)) {
        m_saveCallback(&item, m_callbackUserData);
        m_hasChanges = false;
    } else {
        MessageBox(m_hwnd, TEXT("Invalid item data"), TEXT("Error"), MB_OK | MB_ICONERROR);
    }
}

void ItemView::OnCancelClick()
{
    if (m_hasChanges) {
        if (!ViewHelpers::ShowConfirm(m_hwnd, TEXT("Discard changes"),
                                      TEXT("Discard the changes to this item?"))) {
            return;
        }
    }

    m_hasChanges = false;
    Clear();

    // Hand control back so the user lands on a usable screen; without this the
    // editor stays up with an empty form and no way out.
    if (m_cancelCallback) {
        m_cancelCallback(m_cancelUserData);
    }
}

void ItemView::OnTextChanged()
{
    m_hasChanges = true;
}

void ItemView::LayoutControls()
{
    if (!m_hwnd) {
        return;
    }

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);

    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const int margin = 8;
    const int labelWidth = 78;
    const int gap = 4;
    const int rowHeight = 24;
    const int descHeight = 44;
    const int buttonHeight = 32;

    int editX = margin + labelWidth + gap;
    int editWidth = width - editX - margin;
    if (editWidth < 60) {
        editWidth = 60;
    }

    HWND edits[FIELD_COUNT] = {
        m_barcodeEdit, m_nameEdit, m_descEdit,
        m_locationEdit, m_quantityEdit, m_categoryEdit
    };

    int y = margin;
    for (int i = 0; i < FIELD_COUNT; i++) {
        int h = (i == DESCRIPTION_ROW) ? descHeight : rowHeight;
        if (m_labels[i]) {
            MoveWindow(m_labels[i], margin, y + 3, labelWidth, rowHeight - 4, TRUE);
        }
        if (edits[i]) {
            MoveWindow(edits[i], editX, y, editWidth, h, TRUE);
        }
        y += h + gap;
    }

    // Bottom-anchored: the soft-key menu bar and the navigation bar leave the
    // MC75 barely 268 px of client height, so fixed button coordinates taken
    // from a nominal 320 px form fall off the screen.
    int buttonY = height - buttonHeight - margin;
    if (buttonY < y) {
        buttonY = y;    // very short client area: let the form scroll off instead
    }

    int buttonWidth = (width - (3 * margin)) / 2;
    if (buttonWidth < 50) {
        buttonWidth = 50;
    }

    if (m_saveButton) {
        MoveWindow(m_saveButton, margin, buttonY, buttonWidth, buttonHeight, TRUE);
    }
    if (m_cancelButton) {
        MoveWindow(m_cancelButton, margin + buttonWidth + margin, buttonY,
                   buttonWidth, buttonHeight, TRUE);
    }
}

void ItemView::EnableControls(bool enabled)
{
    if (m_barcodeEdit) EnableWindow(m_barcodeEdit, enabled ? TRUE : FALSE);
    if (m_nameEdit) EnableWindow(m_nameEdit, enabled ? TRUE : FALSE);
    if (m_descEdit) EnableWindow(m_descEdit, enabled ? TRUE : FALSE);
    if (m_locationEdit) EnableWindow(m_locationEdit, enabled ? TRUE : FALSE);
    if (m_quantityEdit) EnableWindow(m_quantityEdit, enabled ? TRUE : FALSE);
    if (m_categoryEdit) EnableWindow(m_categoryEdit, enabled ? TRUE : FALSE);
    if (m_saveButton) EnableWindow(m_saveButton, enabled ? TRUE : FALSE);
}

} // namespace Views
} // namespace HBX
