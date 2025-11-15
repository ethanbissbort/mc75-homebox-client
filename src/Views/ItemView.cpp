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

    // Create main window
    m_hwnd = CreateWindow(
        TEXT("STATIC"),
        TEXT("Item View"),
        WS_CHILD | WS_VISIBLE,
        0, 0, 240, 320,
        parentWnd,
        NULL,
        hInstance,
        NULL
    );

    if (!m_hwnd) {
        return false;
    }

    int yPos = 10;
    int labelHeight = 20;
    int editHeight = 25;
    int spacing = 5;

    // Barcode label and edit
    CreateWindow(TEXT("STATIC"), TEXT("Barcode:"),
        WS_CHILD | WS_VISIBLE, 10, yPos, 80, labelHeight, m_hwnd, NULL, hInstance, NULL);
    m_barcodeEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, yPos, 130, editHeight, m_hwnd, (HMENU)2001, hInstance, NULL);
    yPos += editHeight + spacing;

    // Name label and edit
    CreateWindow(TEXT("STATIC"), TEXT("Name:"),
        WS_CHILD | WS_VISIBLE, 10, yPos, 80, labelHeight, m_hwnd, NULL, hInstance, NULL);
    m_nameEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, yPos, 130, editHeight, m_hwnd, (HMENU)2002, hInstance, NULL);
    yPos += editHeight + spacing;

    // Description label and edit
    CreateWindow(TEXT("STATIC"), TEXT("Description:"),
        WS_CHILD | WS_VISIBLE, 10, yPos, 80, labelHeight, m_hwnd, NULL, hInstance, NULL);
    m_descEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        100, yPos, 130, 50, m_hwnd, (HMENU)2003, hInstance, NULL);
    yPos += 50 + spacing;

    // Location label and edit
    CreateWindow(TEXT("STATIC"), TEXT("Location:"),
        WS_CHILD | WS_VISIBLE, 10, yPos, 80, labelHeight, m_hwnd, NULL, hInstance, NULL);
    m_locationEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, yPos, 130, editHeight, m_hwnd, (HMENU)2004, hInstance, NULL);
    yPos += editHeight + spacing;

    // Quantity label and edit
    CreateWindow(TEXT("STATIC"), TEXT("Quantity:"),
        WS_CHILD | WS_VISIBLE, 10, yPos, 80, labelHeight, m_hwnd, NULL, hInstance, NULL);
    m_quantityEdit = CreateWindow(TEXT("EDIT"), TEXT("0"),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        100, yPos, 130, editHeight, m_hwnd, (HMENU)2005, hInstance, NULL);
    yPos += editHeight + spacing;

    // Category label and edit
    CreateWindow(TEXT("STATIC"), TEXT("Category:"),
        WS_CHILD | WS_VISIBLE, 10, yPos, 80, labelHeight, m_hwnd, NULL, hInstance, NULL);
    m_categoryEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        100, yPos, 130, editHeight, m_hwnd, (HMENU)2006, hInstance, NULL);
    yPos += editHeight + 15;

    // Save button
    m_saveButton = CreateWindow(TEXT("BUTTON"), TEXT("Save"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        30, yPos, 80, 35, m_hwnd, (HMENU)2007, hInstance, NULL);

    // Cancel button
    m_cancelButton = CreateWindow(TEXT("BUTTON"), TEXT("Cancel"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, yPos, 80, 35, m_hwnd, (HMENU)2008, hInstance, NULL);

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

void ItemView::DisplayItem(const Models::Item* item)
{
    if (!item) {
        Clear();
        return;
    }

    if (m_barcodeEdit && item->GetBarcode()) {
        SetWindowText(m_barcodeEdit, item->GetBarcode());
    }

    if (m_nameEdit && item->GetName()) {
        SetWindowText(m_nameEdit, item->GetName());
    }

    if (m_descEdit && item->GetDescription()) {
        SetWindowText(m_descEdit, item->GetDescription());
    }

    if (m_locationEdit && item->GetLocationId()) {
        SetWindowText(m_locationEdit, item->GetLocationId());
    }

    if (m_quantityEdit) {
        TCHAR buffer[32];
        wsprintf(buffer, TEXT("%d"), item->GetQuantity());
        SetWindowText(m_quantityEdit, buffer);
    }

    if (m_categoryEdit && item->GetCategory()) {
        SetWindowText(m_categoryEdit, item->GetCategory());
    }

    m_hasChanges = false;
}

bool ItemView::GetItemData(Models::Item* item) const
{
    if (!item) {
        return false;
    }

    TCHAR buffer[512];

    // Get barcode
    if (m_barcodeEdit) {
        GetWindowText(m_barcodeEdit, buffer, sizeof(buffer) / sizeof(TCHAR));
        item->SetBarcode(buffer);
    }

    // Get name
    if (m_nameEdit) {
        GetWindowText(m_nameEdit, buffer, sizeof(buffer) / sizeof(TCHAR));
        item->SetName(buffer);
    }

    // Get description
    if (m_descEdit) {
        GetWindowText(m_descEdit, buffer, sizeof(buffer) / sizeof(TCHAR));
        item->SetDescription(buffer);
    }

    // Get location
    if (m_locationEdit) {
        GetWindowText(m_locationEdit, buffer, sizeof(buffer) / sizeof(TCHAR));
        item->SetLocationId(buffer);
    }

    // Get quantity
    if (m_quantityEdit) {
        GetWindowText(m_quantityEdit, buffer, sizeof(buffer) / sizeof(TCHAR));
        int quantity = 0;
        // Simple string to int conversion
        for (int i = 0; buffer[i] >= '0' && buffer[i] <= '9'; i++) {
            quantity = quantity * 10 + (buffer[i] - '0');
        }
        item->SetQuantity(quantity);
    }

    // Get category
    if (m_categoryEdit) {
        GetWindowText(m_categoryEdit, buffer, sizeof(buffer) / sizeof(TCHAR));
        item->SetCategory(buffer);
    }

    return item->IsValid();
}

void ItemView::Clear()
{
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
    // Just clear the changes flag and optionally close the view
    m_hasChanges = false;
    Clear();
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
    int margin = 10;
    int labelWidth = 80;
    int editWidth = width - labelWidth - (3 * margin);

    // The controls are already positioned during creation
    // This function could be used to handle dynamic resizing if needed
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
