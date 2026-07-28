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

namespace HBX {

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
        REQUEST_TIMEOUT_MS = 8000
    };

    HINSTANCE m_hInstance;
    HWND m_mainWindow;
    AppState m_state;

    // Core components
    Config* m_config;
    HbClient* m_hbClient;
    SyncEngine* m_syncEngine;
    Journal* m_journal;
    ScannerHAL* m_scanner;

    // UI views (children of the main window)
    Views::ScanView* m_scanView;
    Views::QueueView* m_queueView;
    Views::ItemView* m_itemView;
    HWND m_menuBar;

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
    void ShowScanView();
    void ShowQueueView();
    void ShowItemView(const Models::Item* item, const TCHAR* newBarcode);
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

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Trampolines that forward view callbacks into the controller.
    static void ScanCallbackThunk(const TCHAR* barcode, void* userData);
    static void SyncCallbackThunk(void* userData);
    static void ItemSaveThunk(const Models::Item* item, void* userData);
    static void ItemCancelThunk(void* userData);
    static void QueueChangedThunk(void* userData);

    // Not copyable: the instance owns the components and the window.
    Controller(const Controller&);
    Controller& operator=(const Controller&);
};

} // namespace HBX

#endif // CONTROLLER_HPP
