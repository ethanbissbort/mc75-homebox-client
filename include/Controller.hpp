#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <windows.h>
#include "Config.hpp"
#include "HbClient.hpp"
#include "SyncEngine.hpp"
#include "Journal.hpp"
#include "ScannerHAL.hpp"
#include "Views/ViewHelpers.hpp"
#include "Views/ScanView.hpp"
#include "Views/QueueView.hpp"
#include "Views/ItemView.hpp"
#include "Views/DeviceView.hpp"
#include "Views/PickerView.hpp"

namespace HBX {

// Only ever held by pointer here; the definition is pulled in by Controller.cpp
// so a change to the NetBox client does not rebuild everything that navigates
// through this header.
class NbClient;

/**
 * Main application controller
 * Orchestrates the application flow and coordinates between components
 */
class Controller {
public:
    Controller();
    ~Controller();

    // Application lifecycle
    bool Initialize(HINSTANCE hInstance);
    int Run();
    void Shutdown();

    // State management
    enum AppState {
        STATE_INIT,
        STATE_IDLE,
        STATE_SCANNING,
        STATE_SYNCING,
        STATE_ERROR
    };

    AppState GetState() const;
    void SetState(AppState newState);

    // Component accessors
    Config* GetConfig();
    HbClient* GetHbClient();

    /**
     * The backend new work goes to. Everything that is not HomeBox-specific
     * should go through this rather than through GetHbClient().
     */
    InventoryBackend* GetActiveBackend();

    SyncEngine* GetSyncEngine();
    Journal* GetJournal();
    ScannerHAL* GetScanner();

    // Event handlers
    void OnScanReceived(const TCHAR* barcode);
    void OnSyncRequested();
    void OnConfigChanged();
    void OnItemSave(const Models::Item* item);

private:
    /**
     * Which child view currently fills the client area.
     *
     * A hardware trigger pull has to reach whichever screen is up: the scanner
     * has one sink, and before this existed every decode started a fresh device
     * lookup even while a picker was waiting to be told which rack. The EMDK
     * delivery path is unchanged -- ScanView still marshals the label off the
     * scan thread with PostMessage -- only the destination of the finished
     * label is decided here.
     */
    enum ActiveView {
        VIEW_SCAN,
        VIEW_QUEUE,
        VIEW_ITEM,
        VIEW_DEVICE,
        VIEW_PICKER
    };

    /** What the picker on screen is currently asking. */
    enum PickerPurpose {
        PICK_NONE,
        PICK_BACKEND,        // which inventory system takes new work
        PICK_MATCH,          // one scan resolved to several assets
        PICK_STATUS,         // new device status
        PICK_MOVE_DEST,      // rack / site / location the device is moving to
        PICK_MOVE_POSITION,  // rack unit, possibly a half-U decimal
        PICK_MOVE_FACE       // front or rear
    };

    /**
     * One move being assembled across the picker steps.
     *
     * NetBox validates site, location, rack, position and face against each
     * other -- a rack has to belong to the location, which has to belong to the
     * site -- so a move is sent as a single PATCH carrying the whole positional
     * set. Splitting it into two requests produces a 400 on the second, and
     * replayed from a queue hours later that is an error nobody can act on.
     *
     * Each field is three-state: absent (not touched), present and empty (clear
     * it), or present with a value. That distinction is the difference between
     * "leave the device where it is" and "take it out of the rack".
     */
    struct MoveRequest {
        enum { VALUE_CHARS = 32 };

        TCHAR site[VALUE_CHARS];
        TCHAR location[VALUE_CHARS];
        TCHAR rack[VALUE_CHARS];
        TCHAR position[VALUE_CHARS];
        TCHAR face[VALUE_CHARS];

        bool hasSite;
        bool hasLocation;
        bool hasRack;
        bool hasPosition;
        bool hasFace;
    };

    enum {
        // Drives auto-sync. The tick is deliberately much shorter than the
        // configured sync interval: SyncEngine::ShouldAutoSync owns the real
        // cadence, this only has to give it a chance to say yes.
        TIMER_AUTOSYNC = 1,
        AUTOSYNC_TICK_MS = 15000,

        // Network timeout handed to HbClient. Every server call this class
        // makes is synchronous and most of them run from a UI handler (a scan
        // lookup, an item save), so the transport's 30 second default is 30
        // seconds of dead screen with the operator holding the trigger. Eight
        // seconds still covers a slow GPRS reply - a first request over a cold
        // PDP context routinely takes three to five - while failing over to the
        // offline queue quickly enough that scanning never has to stop.
        REQUEST_TIMEOUT_MS = 8000,

        // Backend-native asset identifier, as carried by Models::AssetSummary.
        TARGET_ID_CHARS = 64,

        // Enough of an asset name to put in a confirmation prompt.
        TARGET_TITLE_CHARS = 48,

        // Bounded scratch for prompts and queue payloads assembled here.
        MESSAGE_CHARS = 256
    };

    HINSTANCE m_hInstance;
    HWND m_mainWindow;
    AppState m_state;

    // Core components
    Config* m_config;
    HbClient* m_hbClient;

    // Created only when hb_conf.json names a NetBox server. NULL otherwise, and
    // then never registered: a backend with no URL cannot answer a lookup, and
    // registering one would let activeBackend select it and silently stop every
    // scan from resolving.
    NbClient* m_nbClient;

    SyncEngine* m_syncEngine;
    Journal* m_journal;
    ScannerHAL* m_scanner;

    // UI views (children of the main window)
    Views::ScanView* m_scanView;
    Views::QueueView* m_queueView;
    Views::ItemView* m_itemView;
    Views::DeviceView* m_deviceView;
    Views::PickerView* m_pickerView;
    HWND m_menuBar;

    ActiveView m_activeView;
    PickerPurpose m_pickerPurpose;

    // The asset the current action applies to, captured when the action starts
    // so a later screen cannot act on something the operator has since scanned
    // over.
    TCHAR m_targetId[TARGET_ID_CHARS];
    TCHAR m_targetTitle[TARGET_TITLE_CHARS];

    // Where the displayed device currently lives, as backend ids rather than
    // names. NetBox validates a rack against the device's site and location, so
    // a rack move has to send all three in the one PATCH, and these are what
    // spare it a second round trip to look them up again. Read from the record
    // the lookup already parsed; empty when the backend has no such notion.
    TCHAR m_targetSiteId[MoveRequest::VALUE_CHARS];
    TCHAR m_targetLocationId[MoveRequest::VALUE_CHARS];

    MoveRequest m_move;

    // Set when another copy of the application already owns the instance mutex;
    // this one activates the running instance and exits without touching the
    // journal or the scanner.
    bool m_secondInstance;
    HANDLE m_instanceMutex;

    bool m_autoSyncTimerRunning;

    // Set while a handler is inside a blocking server call. Windows Mobile
    // pumps messages inside MessageBox, so the auto-sync timer can fire while
    // a modal dialog is up; without this the timer would start a second
    // synchronous request on the one shared HbClient.
    bool m_busy;

    // "Assign location" mode: the next scan names the item, the one after it
    // names the location it moved to.
    bool m_locationMode;
    TCHAR* m_pendingItemBarcode;

    // UI management
    bool InitializeUI();
    void UpdateUI();
    bool CreateMainWindow();
    bool CreateViews();
    bool CreateMenuBar();
    void LayoutViews();

    /**
     * Makes exactly one view visible and records which it is. Every navigation
     * goes through here: with five views, hiding the others by hand at each
     * call site is a mistake factory, and one forgotten Show(false) leaves two
     * screens stacked.
     */
    void ShowOnly(ActiveView view);

    void ShowScanView();
    void ShowQueueView();
    void ShowItemView(const Models::Item* item, const TCHAR* newBarcode);

    /**
     * Shows the read-only detail screen for `asset`. `matchIndex` names the
     * same asset within the last lookup, which is how the positional ids behind
     * the displayed names are recovered without a second request.
     */
    void ShowDeviceView(const Models::AssetSummary* asset, int matchIndex);

    void RefreshQueueUI();

    // Lifecycle helpers
    bool AcquireSingleInstance();
    void ReleaseSingleInstance();
    void StartAutoSyncTimer();
    void StopAutoSyncTimer();
    void OnAutoSyncTick();
    void OnActivate(bool active);

    // Server access

    /**
     * Points every configured backend at the servers named in hb_conf.json,
     * registers them with the sync engine and selects the active one. Safe to
     * call again after the configuration is reloaded.
     *
     * Registration is deliberately wider than selection: only one backend takes
     * new work, but the queue can still hold entries for another one, and those
     * only replay if their backend is registered.
     */
    void ConfigureBackends();

    /**
     * Builds and registers the NetBox client, but only once hb_conf.json names
     * a server. Called again on every configuration reload, which re-points the
     * existing client rather than creating a second one.
     */
    void ConfigureNetbox();

    /**
     * The NetBox client when it is the backend taking new work, NULL otherwise.
     *
     * A structured move and a status change are not on InventoryBackend: the
     * neutral interface covers what both systems can do, and neither operation
     * has a HomeBox counterpart. The typed client is reached by comparing
     * pointers with the one this class built, which needs no RTTI and cannot be
     * fooled by a backend that merely reports a similar name.
     */
    NbClient* ActiveNetbox() const;

    bool EnsureAuthenticated(bool force);

    /** Writes the client's current token to hb_conf.json so it survives a restart. */
    void PersistAuthToken();

    /**
     * Answers "was that failure a rejected session, and did a new one arrive?".
     * True only when the last server call came back 401 and re-authentication
     * then succeeded, so a caller may repeat the call exactly once. Anything
     * else - a 404, a dead link, a server that keeps rejecting the credentials -
     * returns false, which is what bounds the retry.
     */
    bool RetryWithFreshToken();

    bool LookupItem(const TCHAR* barcode, Models::Item* item);
    void RunSync(bool interactive);
    void QueueScanForSync(const TCHAR* barcode, const TCHAR* locationId);

    // Location workflow
    void ToggleLocationMode();
    void OnLocationScan(const TCHAR* barcode);
    void ClearPendingLocationScan();

    // ---- asset workflow ---------------------------------------------------

    /** Resolves a scanned code through the active backend and shows the result. */
    void OnAssetScan(const TCHAR* code);

    /**
     * Offers every match from the last lookup. Never auto-selects: a NetBox
     * serial is not unique, so one scan can legitimately name several devices
     * and picking the first is how the wrong machine gets moved.
     */
    void ShowMatchPicker(InventoryBackend* backend, int matchCount);

    /** Explains why the active backend refused, in terms the operator can act on. */
    void ReportBackendFailure(const TCHAR* what);

    void OnDeviceAction(int action);

    /**
     * Snapshots the asset the detail screen is showing, so a mutation started
     * now cannot be applied to something the operator scanned over while the
     * picker was up. False when there is nothing addressable on screen.
     */
    bool CaptureActionTarget();

    /** Goes back to the detail screen carrying a one-line result. */
    void ReturnToDevice(const TCHAR* notice);

    // Status change
    void BeginStatusChange();
    void ApplyStatus(const TCHAR* statusValue);

    // Move
    void BeginMove();
    void CancelPendingMove();
    void AskMoveDestination();
    void AskMovePosition();
    void AskMoveFace();

    /**
     * Interprets a scanned or typed destination: an "NBRACK:12" style printed
     * label, a NetBox object URL, or a bare integer taken as a rack id.
     * Returns false when the token names nothing this workflow can move to.
     */
    bool ApplyDestinationToken(const TCHAR* token);

    void CommitMove();

    // Picker plumbing
    void ShowPicker(PickerPurpose purpose);
    void OnPickerChoice(const TCHAR* key, bool fromList);
    void OnPickerCancel();

    // Backend selection
    void ShowBackendPicker();
    void SwitchBackend(const TCHAR* instanceId);

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Trampolines that forward view callbacks into the controller.
    static void ScanCallbackThunk(const TCHAR* barcode, void* userData);
    static void SyncCallbackThunk(void* userData);
    static void ItemSaveThunk(const Models::Item* item, void* userData);
    static void ItemCancelThunk(void* userData);
    static void QueueChangedThunk(void* userData);
    static void DeviceActionThunk(int action, void* userData);
    static void PickerChooseThunk(const TCHAR* key, bool fromList, void* userData);
    static void PickerCancelThunk(void* userData);

    // Not copyable: the instance owns the components and the window.
    Controller(const Controller&);
    Controller& operator=(const Controller&);
};

} // namespace HBX

#endif // CONTROLLER_HPP
