#include "../../include/Views/PickerView.hpp"
#include "../../include/Views/ViewHelpers.hpp"
#include "../../include/StrUtil.hpp"
#include <commctrl.h>

// The host shim carries only the list-view surface the original views used; the
// real <commctrl.h> and <windows.h> define all of these, so every guard below
// is a no-op on the device build. LVN_KEYDOWN is the discriminator for the
// notification struct too: a struct cannot be tested with the preprocessor, but
// the header that declares NMLVKEYDOWN is exactly the one that defines
// LVN_KEYDOWN.
#ifndef LVM_SETITEMTEXT
#define LVM_SETITEMTEXT (LVM_FIRST + 116)
#endif
#ifndef ListView_SetItemText
#define ListView_SetItemText(w, i, iSub, txt) \
    do { LVITEM _lvi = {0}; _lvi.iSubItem = (iSub); _lvi.pszText = (txt); \
         SendMessage((w), LVM_SETITEMTEXT, (WPARAM)(int)(i), (LPARAM)&_lvi); } while (0)
#endif
#ifndef LVN_KEYDOWN
#define LVN_KEYDOWN (LVN_FIRST - 55)
typedef struct tagNMLVKEYDOWN {
    NMHDR hdr;
    WORD  wVKey;
    UINT  flags;
} NMLVKEYDOWN;
#endif
#ifndef VK_NUMPAD1
#define VK_NUMPAD1 0x61
#define VK_NUMPAD9 0x69
#endif

namespace HBX {
namespace Views {

namespace {

// Layout constants, matching the other views so the screens do not jump.
const int kMargin       = 8;
const int kLineHeight   = 18;
const int kEditHeight   = 24;
const int kButtonHeight = 30;
const int kGap          = 4;

/** Strips leading and trailing blanks in place; a keypad makes them easy. */
void TrimInPlace(TCHAR* text)
{
    if (!text) {
        return;
    }

    int start = 0;
    while (text[start] == (TCHAR)' ' || text[start] == (TCHAR)'\t') {
        start++;
    }

    int end = start;
    int lastNonBlank = start - 1;
    while (text[end] != 0) {
        if (text[end] != (TCHAR)' ' && text[end] != (TCHAR)'\t' &&
            text[end] != (TCHAR)'\r' && text[end] != (TCHAR)'\n') {
            lastNonBlank = end;
        }
        end++;
    }

    int len = lastNonBlank - start + 1;
    if (len < 0) {
        len = 0;
    }
    for (int i = 0; i < len; i++) {
        text[i] = text[start + i];
    }
    text[len] = 0;
}

} // namespace

PickerView::PickerView()
    : m_hwnd(NULL)
    , m_promptLabel(NULL)
    , m_hintLabel(NULL)
    , m_inputEdit(NULL)
    , m_listView(NULL)
    , m_okButton(NULL)
    , m_cancelButton(NULL)
    , m_hInstance(NULL)
    , m_choiceCount(0)
    , m_selectedIndex(-1)
    , m_mruCount(0)
    , m_chooseCallback(NULL)
    , m_chooseUserData(NULL)
    , m_cancelCallback(NULL)
    , m_cancelUserData(NULL)
{
    m_prompt[0] = 0;
    m_purpose[0] = 0;
}

PickerView::~PickerView()
{
    Destroy();
}

bool PickerView::Create(HWND parentWnd, HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Register a window class that installs PickerView::WindowProc so button
    // clicks and the list's keyboard notifications reach this instance.
    static const TCHAR* kClassName = TEXT("HBXPickerView");
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = PickerView::WindowProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClass(&wc); // harmless if already registered by a prior Create()

    m_hwnd = CreateWindow(
        kClassName,
        TEXT("Picker View"),
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

    m_promptLabel = CreateWindow(TEXT("STATIC"), TEXT(""),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kMargin, kMargin, 224, kLineHeight, m_hwnd, (HMENU)ID_PROMPT_LABEL, hInstance, NULL);

    m_hintLabel = CreateWindow(TEXT("STATIC"), TEXT(""),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        kMargin, 28, 224, kLineHeight, m_hwnd, (HMENU)ID_HINT_LABEL, hInstance, NULL);

    // Deliberately not ES_NUMBER. A rack position is a decimal - NetBox models
    // half-U slots as 42.5 - and ES_NUMBER would silently refuse the '.', which
    // looks like a broken keypad rather than a rejected character.
    m_inputEdit = CreateWindow(TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL,
        kMargin, 48, 224, kEditHeight, m_hwnd, (HMENU)ID_INPUT_EDIT, hInstance, NULL);

    if (m_inputEdit) {
        SendMessage(m_inputEdit, EM_LIMITTEXT, (WPARAM)(INPUT_MAX - 1), 0);
    }

    m_listView = CreateWindow(
        WC_LISTVIEW,
        TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        kMargin, 78, 224, 120,
        m_hwnd,
        (HMENU)ID_LISTVIEW,
        hInstance,
        NULL
    );

    m_okButton = CreateWindow(TEXT("BUTTON"), TEXT("OK"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        kMargin, 230, 100, kButtonHeight, m_hwnd, (HMENU)ID_OK_BUTTON, hInstance, NULL);

    m_cancelButton = CreateWindow(TEXT("BUTTON"), TEXT("Cancel"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 230, 100, kButtonHeight, m_hwnd, (HMENU)ID_CANCEL_BUTTON, hInstance, NULL);

    InitializeListView();
    SetInputHint(NULL);
    LayoutControls();

    return true;
}

void PickerView::Show(bool visible)
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
    }
}

void PickerView::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

HWND PickerView::GetHandle() const
{
    return m_hwnd;
}

void PickerView::SetChooseCallback(ChooseCallback callback, void* userData)
{
    m_chooseCallback = callback;
    m_chooseUserData = userData;
}

void PickerView::SetCancelCallback(CancelCallback callback, void* userData)
{
    m_cancelCallback = callback;
    m_cancelUserData = userData;
}

// ---------------------------------------------------------------------------
// Building a question
// ---------------------------------------------------------------------------

void PickerView::BeginChoices(const TCHAR* prompt, const TCHAR* purpose)
{
    m_choiceCount = 0;
    m_selectedIndex = -1;

    Str::Copy(m_prompt, PROMPT_MAX, prompt ? prompt : TEXT(""));
    Str::Copy(m_purpose, PURPOSE_MAX, purpose ? purpose : TEXT(""));

    if (m_promptLabel) {
        SetWindowText(m_promptLabel, m_prompt);
    }
    if (m_inputEdit) {
        SetWindowText(m_inputEdit, TEXT(""));
    }
    if (m_listView) {
        ListView_DeleteAllItems(m_listView);
    }
}

bool PickerView::AddChoice(const TCHAR* key, const TCHAR* label,
                           const TCHAR* detail, const TCHAR* code)
{
    if (!key || key[0] == 0) {
        return false;
    }
    if (m_choiceCount >= MAX_CHOICES) {
        return false;
    }

    // A key already offered is not added twice. Remembered choices are merged
    // with caller-supplied ones, and the same rack appearing as two rows would
    // make the numbering lie about how many destinations there are.
    for (int i = 0; i < m_choiceCount; i++) {
        if (lstrcmp(m_choices[i].key, key) == 0) {
            return true;
        }
    }

    Choice* slot = &m_choices[m_choiceCount];
    Str::Copy(slot->key, KEY_MAX, key);
    Str::Copy(slot->label, LABEL_MAX, (label && label[0] != 0) ? label : key);
    Str::Copy(slot->detail, DETAIL_MAX, detail ? detail : TEXT(""));
    Str::Copy(slot->code, CODE_MAX, code ? code : TEXT(""));
    m_choiceCount++;

    return true;
}

void PickerView::EndChoices()
{
    OfferRememberedChoices();
    OrderChoicesByHistory();
    PopulateList();
}

int PickerView::GetChoiceCount() const
{
    return m_choiceCount;
}

void PickerView::SetInputHint(const TCHAR* hint)
{
    if (m_hintLabel) {
        SetWindowText(m_hintLabel, (hint && hint[0] != 0) ? hint
                                                          : TEXT("Scan, or type a row number"));
    }
}

void PickerView::SetMessage(const TCHAR* message)
{
    if (!m_promptLabel) {
        return;
    }
    SetWindowText(m_promptLabel, (message && message[0] != 0) ? message : m_prompt);
}

// ---------------------------------------------------------------------------
// Choosing
// ---------------------------------------------------------------------------

bool PickerView::SelectByCode(const TCHAR* code)
{
    if (!code || code[0] == 0 || m_choiceCount == 0) {
        return false;
    }

    // A printed destination label is the fastest path and the one the
    // deployment guide asks sites to use, so it is tried first.
    for (int i = 0; i < m_choiceCount; i++) {
        if (m_choices[i].code[0] != 0 && lstrcmp(m_choices[i].code, code) == 0) {
            ActivateRow(i);
            return true;
        }
    }

    // A bare row number, which is what the operator types on the keypad.
    if (code[1] == 0 && code[0] >= (TCHAR)'1' && code[0] <= (TCHAR)'9') {
        int row = (int)(code[0] - (TCHAR)'1');
        if (row < m_choiceCount) {
            ActivateRow(row);
            return true;
        }
    }

    // Finally the key itself, so typing "front" picks the Front row.
    for (int i = 0; i < m_choiceCount; i++) {
        if (lstrcmp(m_choices[i].key, code) == 0) {
            ActivateRow(i);
            return true;
        }
    }

    return false;
}

// static
int PickerView::RowFromVirtualKey(int vkey)
{
    // Both ranges are handled because MC75 keypads differ in which one they
    // emit for the same physical digit.
    if (vkey >= (int)'1' && vkey <= (int)'9') {
        return vkey - (int)'1';
    }
    if (vkey >= VK_NUMPAD1 && vkey <= VK_NUMPAD9) {
        return vkey - VK_NUMPAD1;
    }
    return -1;
}

void PickerView::ActivateRow(int index)
{
    if (index < 0 || index >= m_choiceCount) {
        return;
    }

    RememberChoice(index);

    // The key is copied out first: the callback typically starts the next
    // question, which rewrites m_choices underneath us.
    TCHAR key[KEY_MAX];
    Str::Copy(key, KEY_MAX, m_choices[index].key);

    RaiseChoice(key, true);
}

void PickerView::RaiseChoice(const TCHAR* key, bool fromList)
{
    if (m_chooseCallback) {
        m_chooseCallback(key, fromList, m_chooseUserData);
    }
}

void PickerView::OnOkClick()
{
    TCHAR typed[INPUT_MAX];
    typed[0] = 0;
    if (m_inputEdit) {
        GetWindowText(m_inputEdit, typed, INPUT_MAX);
    }
    TrimInPlace(typed);

    if (typed[0] == 0) {
        if (m_selectedIndex >= 0 && m_selectedIndex < m_choiceCount) {
            ActivateRow(m_selectedIndex);
        } else {
            SetMessage(TEXT("Pick a row, or type a value"));
        }
        return;
    }

    if (SelectByCode(typed)) {
        return;
    }

    // Not one of the rows. The caller decides what an unrecognised entry means
    // here - for a rack it is an id to resolve and confirm, for a position it
    // is the value itself - so it is handed over rather than rejected.
    RaiseChoice(typed, false);
}

void PickerView::OnCancelClick()
{
    if (m_cancelCallback) {
        m_cancelCallback(m_cancelUserData);
    }
}

void PickerView::OnItemSelected(int index)
{
    m_selectedIndex = (index >= 0 && index < m_choiceCount) ? index : -1;
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

PickerView::MruBucket* PickerView::FindBucket(const TCHAR* purpose)
{
    if (!purpose || purpose[0] == 0) {
        return NULL;
    }
    for (int i = 0; i < m_mruCount; i++) {
        if (lstrcmp(m_mru[i].purpose, purpose) == 0) {
            return &m_mru[i];
        }
    }
    return NULL;
}

PickerView::MruBucket* PickerView::FindOrCreateBucket(const TCHAR* purpose)
{
    MruBucket* bucket = FindBucket(purpose);
    if (bucket) {
        return bucket;
    }
    if (!purpose || purpose[0] == 0 || m_mruCount >= MRU_PURPOSES) {
        return NULL;
    }

    bucket = &m_mru[m_mruCount++];
    Str::Copy(bucket->purpose, PURPOSE_MAX, purpose);
    bucket->count = 0;
    return bucket;
}

int PickerView::MruRank(const MruBucket* bucket, const TCHAR* key) const
{
    if (!bucket || !key) {
        return -1;
    }
    for (int i = 0; i < bucket->count; i++) {
        if (lstrcmp(bucket->entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

void PickerView::RememberEntry(const TCHAR* key, const TCHAR* label, const TCHAR* detail)
{
    if (!key || key[0] == 0) {
        return;
    }

    MruBucket* bucket = FindOrCreateBucket(m_purpose);
    if (!bucket) {
        return;   // history is off for this question
    }

    // Move-to-front. The existing copy is dropped first so the same rack does
    // not occupy two of the remembered slots.
    int existing = MruRank(bucket, key);
    if (existing >= 0) {
        for (int i = existing; i + 1 < bucket->count; i++) {
            bucket->entries[i] = bucket->entries[i + 1];
        }
        bucket->count--;
    } else if (bucket->count == MRU_DEPTH) {
        bucket->count--;   // drop the oldest
    }

    for (int i = bucket->count; i > 0; i--) {
        bucket->entries[i] = bucket->entries[i - 1];
    }

    Str::Copy(bucket->entries[0].key, KEY_MAX, key);
    Str::Copy(bucket->entries[0].label, LABEL_MAX, (label && label[0] != 0) ? label : key);
    Str::Copy(bucket->entries[0].detail, DETAIL_MAX, detail ? detail : TEXT(""));
    bucket->count++;
}

void PickerView::RememberChoice(int index)
{
    if (index < 0 || index >= m_choiceCount) {
        return;
    }
    RememberEntry(m_choices[index].key, m_choices[index].label, m_choices[index].detail);
}

void PickerView::Remember(const TCHAR* key, const TCHAR* label, const TCHAR* detail)
{
    RememberEntry(key, label, detail);
}

void PickerView::OfferRememberedChoices()
{
    const MruBucket* bucket = FindBucket(m_purpose);
    if (!bucket) {
        return;
    }

    // A remembered destination is re-offered even when the caller did not know
    // about it: on a handheld the caller usually has no list to supply at all,
    // and the shift's working set is the only thing that makes the picker
    // useful before the first scan.
    for (int i = 0; i < bucket->count && m_choiceCount < MAX_CHOICES; i++) {
        AddChoice(bucket->entries[i].key, bucket->entries[i].label,
                  bucket->entries[i].detail, NULL);
    }
}

void PickerView::OrderChoicesByHistory()
{
    const MruBucket* bucket = FindBucket(m_purpose);
    if (!bucket || m_choiceCount < 2) {
        return;
    }

    // Rank: remembered entries first in history order, everything else after in
    // the order the caller added it. Ranks are unique, so a plain selection
    // sort is stable here, and MAX_CHOICES is 9.
    int rank[MAX_CHOICES];
    for (int i = 0; i < m_choiceCount; i++) {
        int mru = MruRank(bucket, m_choices[i].key);
        rank[i] = (mru >= 0) ? mru : (MRU_DEPTH + i);
    }

    for (int i = 0; i < m_choiceCount - 1; i++) {
        int best = i;
        for (int j = i + 1; j < m_choiceCount; j++) {
            if (rank[j] < rank[best]) {
                best = j;
            }
        }
        if (best != i) {
            Choice tmpChoice = m_choices[i];
            m_choices[i] = m_choices[best];
            m_choices[best] = tmpChoice;

            int tmpRank = rank[i];
            rank[i] = rank[best];
            rank[best] = tmpRank;
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void PickerView::PopulateList()
{
    if (!m_listView) {
        return;
    }

    ListView_DeleteAllItems(m_listView);

    for (int i = 0; i < m_choiceCount; i++) {
        TCHAR number[8];
        number[0] = 0;
        Str::AppendInt(number, 8, (long)(i + 1));

        LVITEM item = {0};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = number;

        int inserted = ListView_InsertItem(m_listView, &item);
        if (inserted >= 0) {
            ListView_SetItemText(m_listView, inserted, 1, m_choices[i].label);
            ListView_SetItemText(m_listView, inserted, 2, m_choices[i].detail);
        }
    }

    // Row 1 is the most-recently-used destination, so pre-selecting it makes
    // "the same place as last time" a single OK press.
    m_selectedIndex = (m_choiceCount > 0) ? 0 : -1;
}

LRESULT CALLBACK PickerView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PickerView* pThis = NULL;

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (PickerView*)pCreate->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)pThis);
    } else {
        pThis = (PickerView*)GetWindowLong(hwnd, GWL_USERDATA);
    }

    switch (uMsg) {
        case WM_COMMAND:
            if (pThis && HIWORD(wParam) == BN_CLICKED) {
                if (LOWORD(wParam) == ID_OK_BUTTON) {
                    pThis->OnOkClick();
                    return 0;
                }
                if (LOWORD(wParam) == ID_CANCEL_BUTTON) {
                    pThis->OnCancelClick();
                    return 0;
                }
            }
            break;

        case WM_NOTIFY:
            if (pThis) {
                NMHDR* pnmhdr = (NMHDR*)lParam;
                if (pnmhdr->idFrom == ID_LISTVIEW) {
                    if (pnmhdr->code == LVN_ITEMCHANGED) {
                        NMLISTVIEW* pnmlv = (NMLISTVIEW*)lParam;
                        if (pnmlv->uNewState & LVIS_SELECTED) {
                            pThis->OnItemSelected(pnmlv->iItem);
                        }
                    } else if (pnmhdr->code == (UINT)LVN_KEYDOWN) {
                        // Digit keys pick a row outright. This lives on the
                        // list's own notification rather than an accelerator
                        // table because the message loop has no focus filter,
                        // and an accelerator would take every digit away from
                        // the entry field as well.
                        NMLVKEYDOWN* keyDown = (NMLVKEYDOWN*)lParam;
                        int row = RowFromVirtualKey((int)keyDown->wVKey);
                        if (row >= 0 && row < pThis->m_choiceCount) {
                            pThis->ActivateRow(row);
                            return 0;
                        }
                    }
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

void PickerView::LayoutControls()
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

    int contentWidth = width - (2 * kMargin);
    if (contentWidth < 60) {
        contentWidth = 60;
    }

    int y = kMargin;
    if (m_promptLabel) {
        MoveWindow(m_promptLabel, kMargin, y, contentWidth, kLineHeight, TRUE);
    }
    y += kLineHeight + 2;

    if (m_hintLabel) {
        MoveWindow(m_hintLabel, kMargin, y, contentWidth, kLineHeight, TRUE);
    }
    y += kLineHeight + 2;

    if (m_inputEdit) {
        MoveWindow(m_inputEdit, kMargin, y, contentWidth, kEditHeight, TRUE);
    }
    y += kEditHeight + kGap;

    // Bottom-anchored buttons; the list takes whatever is left. The soft-input
    // panel can cover the lower half of the screen while the entry field is in
    // use, and WM_SETTINGCHANGE re-runs this with the smaller client area.
    int buttonY = height - kButtonHeight - kMargin;

    int listHeight = buttonY - y - kGap;
    if (listHeight < 40) {
        listHeight = 40;
    }
    if (m_listView) {
        MoveWindow(m_listView, kMargin, y, contentWidth, listHeight, TRUE);
    }

    int buttonWidth = (contentWidth - kMargin) / 2;
    if (buttonWidth < 50) {
        buttonWidth = 50;
    }

    if (m_okButton) {
        MoveWindow(m_okButton, kMargin, buttonY, buttonWidth, kButtonHeight, TRUE);
    }
    if (m_cancelButton) {
        MoveWindow(m_cancelButton, kMargin + buttonWidth + kMargin, buttonY,
                   buttonWidth, kButtonHeight, TRUE);
    }
}

void PickerView::InitializeListView()
{
    if (!m_listView) {
        return;
    }

    ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN column = {0};
    column.mask = LVCF_TEXT | LVCF_WIDTH;

    // The number column carries the keypad digit, so it only has to fit one
    // character - every other pixel belongs to telling two candidates apart.
    column.pszText = (TCHAR*)TEXT("#");
    column.cx = 20;
    ListView_InsertColumn(m_listView, 0, &column);

    column.pszText = (TCHAR*)TEXT("Name");
    column.cx = 118;
    ListView_InsertColumn(m_listView, 1, &column);

    column.pszText = (TCHAR*)TEXT("Where");
    column.cx = 80;
    ListView_InsertColumn(m_listView, 2, &column);
}

} // namespace Views
} // namespace HBX
