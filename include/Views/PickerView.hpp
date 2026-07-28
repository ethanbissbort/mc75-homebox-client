#ifndef VIEWS_PICKERVIEW_HPP
#define VIEWS_PICKERVIEW_HPP

#include <windows.h>
#include "ViewHelpers.hpp"

namespace HBX {
namespace Views {

/**
 * One generic chooser, used for every "which one?" question the client asks.
 *
 * Two callers justify it being generic rather than a pair of purpose-built
 * screens:
 *
 *  - Disambiguation. NetBox does not enforce uniqueness on a device serial, so
 *    one scan can legitimately resolve to several devices, and the operator has
 *    to say which. Auto-picking the first is how the wrong machine gets moved.
 *  - Destination selection - site, location, rack, rack face, device status,
 *    and which backend is active. All of them are "pick one of a short list".
 *
 * Three input paths, in the order they are actually used at a rack:
 *
 *  1. A scan. SelectByCode() matches the decoded label against each row's
 *     scan code and chooses it outright, which is the whole point of printing
 *     rack labels.
 *  2. A digit. Rows are numbered 1-9 and LVN_KEYDOWN accepts both the '1'-'9'
 *     and the VK_NUMPAD1-9 ranges, because MC75 keypads differ in which they
 *     emit. This is done from the list's own notification rather than with an
 *     accelerator table: the message loop has no focus filter, so an
 *     accelerator would steal every digit from the entry field as well.
 *  3. The entry field, for an id that has no label and no row.
 *
 * Rows are ordered most-recently-used first per purpose, and a remembered
 * choice is re-offered even when the caller did not supply it, so the rack the
 * operator has been working in all morning is row 1 rather than something to
 * hunt for. The history is per-process: it is a working set for the shift, not
 * a persisted cache that could go stale against the server.
 */
class PickerView {
public:
    enum {
        /** Rows offered at once; one keypad digit each, and 0 is not a row. */
        MAX_CHOICES = 9,

        KEY_MAX    = 48,
        LABEL_MAX  = 40,
        DETAIL_MAX = 40,
        CODE_MAX   = 64,
        PROMPT_MAX = 64,
        INPUT_MAX  = 64,

        /** Distinct question kinds that keep a history (rack, position, ...). */
        MRU_PURPOSES = 6,
        MRU_DEPTH    = 6,
        PURPOSE_MAX  = 16
    };

    PickerView();
    ~PickerView();

    // Window management
    bool Create(HWND parentWnd, HINSTANCE hInstance);
    void Show(bool visible);
    void Destroy();
    HWND GetHandle() const;

    /**
     * Starts a new question. `prompt` is the line above the list. `purpose`
     * names the history bucket and may be NULL, which disables history
     * entirely -- that is what disambiguation wants, since the server's order
     * carries meaning and the ids never repeat.
     */
    void BeginChoices(const TCHAR* prompt, const TCHAR* purpose);

    /**
     * Adds one row. `key` is what comes back through the callback, `label` and
     * `detail` are the two visible columns, and `code` (may be NULL) is a
     * scanned label that selects this row outright. Returns false once
     * MAX_CHOICES rows have been added.
     */
    bool AddChoice(const TCHAR* key, const TCHAR* label, const TCHAR* detail, const TCHAR* code);

    /** Applies the history ordering and fills the list. */
    void EndChoices();

    /** Number of rows currently offered. */
    int GetChoiceCount() const;

    /**
     * Text shown above the entry field. Use it to say what may be typed --
     * "Rack id, or a row number". NULL restores the default.
     */
    void SetInputHint(const TCHAR* hint);

    /**
     * Offers `code` to the rows: an exact match on a row's scan code, or a bare
     * row number, chooses that row and raises the callback. Returns false when
     * nothing matched, leaving the caller to decide what an unrecognised token
     * means for this particular question.
     */
    bool SelectByCode(const TCHAR* code);

    /**
     * Shows a message in place of the prompt without disturbing the rows, so a
     * rejected entry ("Not a rack id") can be reported without a modal that
     * would swallow the next trigger pull.
     */
    void SetMessage(const TCHAR* message);

    /**
     * Records a choice the caller resolved for itself -- a scanned rack label,
     * a typed id -- against the current question's history, so it is offered as
     * a row the next time that question is asked. Without it the history would
     * only ever learn answers that were already on screen, which is exactly the
     * set the operator did not need help with.
     */
    void Remember(const TCHAR* key, const TCHAR* label, const TCHAR* detail);

    /**
     * Raised when the operator settles on something. `fromList` is true when
     * `key` is one of the offered rows; it is false when the operator typed or
     * scanned something the picker did not recognise, in which case the caller
     * owns resolving it -- and must confirm what it resolved to before acting,
     * because a mistyped id is otherwise indistinguishable from a correct one.
     */
    typedef void (*ChooseCallback)(const TCHAR* key, bool fromList, void* userData);
    void SetChooseCallback(ChooseCallback callback, void* userData);

    typedef void (*CancelCallback)(void* userData);
    void SetCancelCallback(CancelCallback callback, void* userData);

private:
    enum {
        ID_PROMPT_LABEL = 5001,
        ID_HINT_LABEL   = 5002,
        ID_INPUT_EDIT   = 5003,
        ID_LISTVIEW     = 5004,
        ID_OK_BUTTON    = 5005,
        ID_CANCEL_BUTTON = 5006
    };

    struct Choice {
        TCHAR key[KEY_MAX];
        TCHAR label[LABEL_MAX];
        TCHAR detail[DETAIL_MAX];
        TCHAR code[CODE_MAX];
    };

    /** One remembered choice, kept whole so it can be re-offered as a row. */
    struct MruEntry {
        TCHAR key[KEY_MAX];
        TCHAR label[LABEL_MAX];
        TCHAR detail[DETAIL_MAX];
    };

    struct MruBucket {
        TCHAR purpose[PURPOSE_MAX];
        MruEntry entries[MRU_DEPTH];
        int count;
    };

    HWND m_hwnd;
    HWND m_promptLabel;
    HWND m_hintLabel;
    HWND m_inputEdit;
    HWND m_listView;
    HWND m_okButton;
    HWND m_cancelButton;
    HINSTANCE m_hInstance;

    Choice m_choices[MAX_CHOICES];
    int m_choiceCount;
    int m_selectedIndex;

    TCHAR m_prompt[PROMPT_MAX];
    TCHAR m_purpose[PURPOSE_MAX];

    MruBucket m_mru[MRU_PURPOSES];
    int m_mruCount;

    ChooseCallback m_chooseCallback;
    void* m_chooseUserData;
    CancelCallback m_cancelCallback;
    void* m_cancelUserData;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnOkClick();
    void OnCancelClick();
    void OnItemSelected(int index);

    /** Maps a virtual key to a zero-based row, or -1. */
    static int RowFromVirtualKey(int vkey);

    void ActivateRow(int index);
    void RaiseChoice(const TCHAR* key, bool fromList);

    /** History lookup/insert; both return -1 / do nothing when history is off. */
    MruBucket* FindBucket(const TCHAR* purpose);
    MruBucket* FindOrCreateBucket(const TCHAR* purpose);
    int MruRank(const MruBucket* bucket, const TCHAR* key) const;
    void RememberEntry(const TCHAR* key, const TCHAR* label, const TCHAR* detail);
    void RememberChoice(int index);

    void OfferRememberedChoices();
    void OrderChoicesByHistory();
    void PopulateList();

    void LayoutControls();
    void InitializeListView();

    // Not copyable: the instance owns a window.
    PickerView(const PickerView&);
    PickerView& operator=(const PickerView&);
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_PICKERVIEW_HPP
