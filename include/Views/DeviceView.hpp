#ifndef VIEWS_DEVICEVIEW_HPP
#define VIEWS_DEVICEVIEW_HPP

#include <windows.h>
#include "ViewHelpers.hpp"
#include "../Models/AssetSummary.hpp"

namespace HBX {
namespace Views {

/**
 * Read-only detail screen for one scanned asset.
 *
 * It is driven entirely by Models::AssetSummary, so it renders a HomeBox item
 * and a NetBox device through the same code and needs no backend type at all.
 * That is a correctness decision rather than a tidiness one: ItemView is an
 * editor that rebuilds a whole record from its controls, and pushing a whole
 * record back to NetBox blanks every field the handheld never loaded. A viewer
 * with a small set of targeted actions cannot do that.
 *
 * Nothing here is editable. Each action hands control back to the owner, which
 * runs the mutation as its own narrow flow and sends only the keys it changed.
 *
 * The action buttons are driven by capability flags supplied by the owner, not
 * by which backend is active: a backend that has no status concept simply does
 * not get a Status button, and this class never learns why.
 */
class DeviceView {
public:
    /** Identifies the button the operator pressed, for the action callback. */
    enum Action {
        ACTION_MOVE   = 1,
        ACTION_STATUS = 2,
        ACTION_BACK   = 3
    };

    DeviceView();
    ~DeviceView();

    // Window management
    bool Create(HWND parentWnd, HINSTANCE hInstance);
    void Show(bool visible);
    void Destroy();
    HWND GetHandle() const;

    /**
     * Renders `asset`. The view keeps its own copy, so the caller's summary can
     * go out of scope -- AssetSummary is fixed-size and heap-free, which is
     * what makes a by-value copy per scan cheaper than the alternative.
     */
    void DisplayAsset(const Models::AssetSummary* asset);

    /** The asset currently on screen, or NULL when the view is empty. */
    const Models::AssetSummary* GetAsset() const;

    void Clear();

    /**
     * Which actions the owner can actually carry out for the displayed asset.
     * Hidden buttons are left out of the layout entirely rather than disabled:
     * on 240 px a greyed button is indistinguishable from a live one at arm's
     * length in a warehouse aisle.
     */
    void SetActionsAvailable(bool canMove, bool canChangeStatus);

    /**
     * Replaces the status line with a transient message ("Queued for sync",
     * "Offline - cannot change status"). Passing NULL restores the status.
     * This is deliberately not a MessageBox: Windows Mobile pumps messages
     * inside one, so a modal put up after a scan is a window in which the next
     * trigger pull is silently dropped.
     */
    void SetNotice(const TCHAR* notice);

    /**
     * Raised when an action button is pressed. One callback with an Action
     * rather than one callback per button: the set of actions is decided by
     * the owner's capability flags, so a single entry point keeps the two in
     * step instead of leaving an unused thunk behind for every hidden button.
     */
    typedef void (*ActionCallback)(int action, void* userData);
    void SetActionCallback(ActionCallback callback, void* userData);

private:
    enum {
        ID_LISTVIEW      = 4001,
        ID_MOVE_BUTTON   = 4002,
        ID_STATUS_BUTTON = 4003,
        ID_BACK_BUTTON   = 4004,

        // Header text is composed from several summary fields, so it is built
        // in one bounded buffer rather than formatted with wsprintf.
        HEADER_CHARS = 160
    };

    HWND m_hwnd;
    HWND m_titleLabel;
    HWND m_subtitleLabel;
    HWND m_statusLabel;
    HWND m_listView;
    HWND m_moveButton;
    HWND m_statusButton;
    HWND m_backButton;
    HINSTANCE m_hInstance;

    // Owned; released in the destructor. The title carries the device name and
    // is the one line an operator reads at a glance, so it is the only control
    // worth a font of its own.
    HFONT m_titleFont;

    Models::AssetSummary m_asset;
    bool m_hasAsset;
    bool m_canMove;
    bool m_canChangeStatus;

    ActionCallback m_actionCallback;
    void* m_callbackUserData;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void RaiseAction(int action);
    void PopulateList();
    void RefreshHeader();
    void LayoutControls();
    void InitializeListView();

    // Not copyable: the instance owns a window and a font.
    DeviceView(const DeviceView&);
    DeviceView& operator=(const DeviceView&);
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_DEVICEVIEW_HPP
