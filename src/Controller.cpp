#include "../include/Controller.hpp"
#include "../include/StrUtil.hpp"
#include <commctrl.h>
#include <aygshell.h>
#include "../resources/resource.h"

namespace HBX {

namespace {

// Name of the mutex that marks "an HBXClient is already running". It has to be
// a fixed, process-independent name for the second launch to see it.
const TCHAR* const kInstanceMutexName = TEXT("HBXClientInstanceMutex");
const TCHAR* const kMainWindowClass   = TEXT("HBXClientWndClass");

// The server rejected our token. HbClient reports it here (and drops the
// session) so a failed call can be told apart from a missing record.
const int kHttpUnauthorized = 401;

/** Clears the controller's busy flag however the guarded handler returns. */
class BusyScope {
public:
    explicit BusyScope(bool* flag) : m_flag(flag) { *m_flag = true; }
    ~BusyScope() { *m_flag = false; }
private:
    bool* m_flag;
    BusyScope(const BusyScope&);
    BusyScope& operator=(const BusyScope&);
};

} // namespace

Controller::Controller()
    : m_hInstance(NULL)
    , m_mainWindow(NULL)
    , m_state(STATE_INIT)
    , m_config(NULL)
    , m_hbClient(NULL)
    , m_syncEngine(NULL)
    , m_journal(NULL)
    , m_scanner(NULL)
    , m_scanView(NULL)
    , m_queueView(NULL)
    , m_itemView(NULL)
    , m_menuBar(NULL)
    , m_secondInstance(false)
    , m_instanceMutex(NULL)
    , m_autoSyncTimerRunning(false)
    , m_busy(false)
    , m_locationMode(false)
    , m_pendingItemBarcode(NULL)
{
}

Controller::~Controller()
{
    // Shutdown is idempotent, so calling it here also covers the failure paths
    // in Initialize / CreateViews, where WinMain only deletes the controller
    // and would otherwise leak every component - including a live scan thread.
    Shutdown();
}

bool Controller::Initialize(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // A second copy would fight the first over the journal file (opened without
    // share flags) and over the SCN1: scanner port, so the relaunch hands focus
    // to the running instance and quits quietly instead.
    if (!AcquireSingleInstance()) {
        m_secondInstance = true;
        return true;
    }

    // Initialize common controls. ICC_LISTVIEW_CLASSES is required so the
    // WC_LISTVIEW window class used by QueueView / ViewHelpers is registered;
    // without it CreateWindow(WC_LISTVIEW, ...) can fail and the queue list
    // never appears.
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // Create core components
    m_config = new Config();
    m_journal = new Journal();
    m_hbClient = new HbClient();
    m_scanner = new ScannerHAL();
    m_syncEngine = new SyncEngine(m_hbClient, m_journal);

    // Load configuration from every documented location, not just the install
    // directory, so a card-deployed hb_conf.json is honoured.
    m_config->LoadFromDefaultLocations();

    // Initialize journal
    if (!m_journal->Initialize(m_config->GetJournalPath())) {
        return false;
    }

    // Configure API client. The timeout matters as much as the URL here: the
    // transport's own default would block a scan handler for half a minute on a
    // link that has gone away.
    m_hbClient->SetBaseUrl(m_config->GetApiBaseUrl());
    m_hbClient->SetRequestTimeout((DWORD)REQUEST_TIMEOUT_MS);

    // Initialize scanner
    if (m_scanner->Initialize()) {
        m_scanner->SetBeepEnabled(m_config->IsScannerBeepEnabled());
        m_scanner->SetVibrateEnabled(m_config->IsScannerVibrateEnabled());

        // EnableScanner is what actually issues SCAN_Enable and lets the scan
        // thread start reading labels. Without it neither the soft trigger nor
        // the MC75's physical trigger ever produces a decode.
        if (!m_scanner->EnableScanner()) {
            m_journal->LogError(TEXT("SCANNER_ENABLE"), TEXT("Failed to enable scanner hardware"));
        }
    } else {
        m_journal->LogError(TEXT("SCANNER_INIT"), TEXT("Failed to initialize scanner hardware"));
        // Continue anyway - the manual barcode entry on the scan screen keeps
        // the device usable with a dead or absent scanner.
    }

    // Create main window and UI
    if (!InitializeUI()) {
        return false;
    }

    // Authentication is deliberately not fatal: an MC75 that starts up out of
    // coverage still has to scan and queue. Every server call re-checks this.
    if (!EnsureAuthenticated(false)) {
        m_journal->LogInfo(TEXT("Starting unauthenticated - working offline"));
    }

    m_syncEngine->SetAutoSyncIntervalSeconds(m_config->GetSyncIntervalSeconds());
    m_syncEngine->SetAutoSyncEnabled(m_config->GetSyncIntervalSeconds() > 0);
    StartAutoSyncTimer();

    m_journal->LogInfo(TEXT("Application initialized successfully"));
    SetState(STATE_IDLE);

    return true;
}

int Controller::Run()
{
    // Another instance owns the journal and the scanner; it has already been
    // brought to the front, so there is nothing for this process to do.
    if (m_secondInstance) {
        return 0;
    }

    MSG msg;

    // Show main window
    ShowWindow(m_mainWindow, SW_SHOW);
    UpdateWindow(m_mainWindow);

    // Message loop
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

void Controller::Shutdown()
{
    StopAutoSyncTimer();

    // The EMDK scan thread calls straight into ScanView, so it has to be
    // detached and stopped BEFORE any view is destroyed. A label decoded during
    // teardown would otherwise be delivered to freed memory - the read loop can
    // have a SCAN_ReadLabelWait in flight for up to a second.
    if (m_scanner) {
        m_scanner->SetScanCallback(NULL, NULL);
        m_scanner->DisableScanner();
        m_scanner->Shutdown();
        delete m_scanner;
        m_scanner = NULL;
    }

    // Tear down the UI views (they hold non-owning references to the sync
    // engine, so they go before it is deleted).
    if (m_scanView) {
        delete m_scanView;
        m_scanView = NULL;
    }
    if (m_queueView) {
        delete m_queueView;
        m_queueView = NULL;
    }
    if (m_itemView) {
        delete m_itemView;
        m_itemView = NULL;
    }

    // Cleanup components
    if (m_syncEngine) {
        delete m_syncEngine;
        m_syncEngine = NULL;
    }

    if (m_hbClient) {
        delete m_hbClient;
        m_hbClient = NULL;
    }

    if (m_journal) {
        m_journal->LogInfo(TEXT("Application shutdown"));
        delete m_journal;
        m_journal = NULL;
    }

    if (m_config) {
        delete m_config;
        m_config = NULL;
    }

    ClearPendingLocationScan();

    // Destroy main window
    if (m_mainWindow) {
        DestroyWindow(m_mainWindow);
        m_mainWindow = NULL;
    }

    ReleaseSingleInstance();
}

Controller::AppState Controller::GetState() const
{
    return m_state;
}

void Controller::SetState(AppState newState)
{
    m_state = newState;
    UpdateUI();
}

Config* Controller::GetConfig()
{
    return m_config;
}

HbClient* Controller::GetHbClient()
{
    return m_hbClient;
}

SyncEngine* Controller::GetSyncEngine()
{
    return m_syncEngine;
}

Journal* Controller::GetJournal()
{
    return m_journal;
}

ScannerHAL* Controller::GetScanner()
{
    return m_scanner;
}

bool Controller::AcquireSingleInstance()
{
    m_instanceMutex = CreateMutex(NULL, FALSE, kInstanceMutexName);
    if (m_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindow(kMainWindowClass, NULL);
        if (existing) {
            // Windows CE requires the low bit of the handle to be set to force
            // the window out of the background.
            SetForegroundWindow((HWND)((UINT_PTR)existing | 1));
        }
        CloseHandle(m_instanceMutex);
        m_instanceMutex = NULL;
        return false;
    }
    return true;
}

void Controller::ReleaseSingleInstance()
{
    if (m_instanceMutex) {
        ReleaseMutex(m_instanceMutex);
        CloseHandle(m_instanceMutex);
        m_instanceMutex = NULL;
    }
}

void Controller::StartAutoSyncTimer()
{
    if (!m_mainWindow || m_autoSyncTimerRunning) {
        return;
    }
    if (SetTimer(m_mainWindow, (UINT_PTR)TIMER_AUTOSYNC, (UINT)AUTOSYNC_TICK_MS, NULL)) {
        m_autoSyncTimerRunning = true;
    }
}

void Controller::StopAutoSyncTimer()
{
    if (m_mainWindow && m_autoSyncTimerRunning) {
        KillTimer(m_mainWindow, (UINT_PTR)TIMER_AUTOSYNC);
    }
    m_autoSyncTimerRunning = false;
}

void Controller::OnAutoSyncTick()
{
    // Never cut across a scan lookup or a sync that is already running: both
    // drive the one HbClient, and both are synchronous.
    if (m_busy || m_state != STATE_IDLE || !m_syncEngine) {
        return;
    }
    if (!m_syncEngine->ShouldAutoSync(GetTickCount())) {
        return;
    }

    RunSync(false);
}

void Controller::OnActivate(bool active)
{
    // Keeping the beam hardware armed while the device sits in a holster is
    // what empties an MC75 battery overnight, so follow the activation state.
    if (!m_scanner || !m_scanner->IsInitialized()) {
        return;
    }

    if (active) {
        m_scanner->EnableScanner();
    } else {
        m_scanner->DisableScanner();
    }
}

bool Controller::EnsureAuthenticated(bool force)
{
    if (!m_hbClient || !m_config) {
        return false;
    }

    if (!force && m_hbClient->IsAuthenticated()) {
        return true;
    }

    // Prefer a token saved by an earlier run. A battery swap ends this process
    // several times a shift, and the restart that happens in the back of a
    // warehouse cannot complete a handshake at all - it would come up
    // unauthenticated, and queue every scan, while holding a perfectly good
    // token. Restoring costs no round trip; if the server has since retired the
    // token the first 401 drops it and the caller forces the branch below.
    if (!force) {
        const TCHAR* storedToken = m_config->GetAuthToken();
        if (storedToken && lstrlen(storedToken) > 0) {
            m_hbClient->SetAuthToken(m_config->GetDeviceId(), storedToken);
            m_journal->LogInfo(TEXT("Restored saved session"));
            return true;
        }
    }

    const TCHAR* apiKey = m_config->GetApiKey();
    if (!apiKey || lstrlen(apiKey) == 0) {
        m_journal->LogError(TEXT("AUTH_NO_KEY"),
                            TEXT("No apiKey in hb_conf.json; server calls will be rejected"));
        return false;
    }

    if (!m_hbClient->Authenticate(m_config->GetDeviceId(), apiKey)) {
        m_journal->LogError(TEXT("AUTH_FAILED"), TEXT("Device authentication was rejected"));
        return false;
    }

    m_journal->LogInfo(TEXT("Device authenticated"));
    PersistAuthToken();
    return true;
}

void Controller::PersistAuthToken()
{
    if (!m_config || !m_hbClient) {
        return;
    }

    const TCHAR* token = m_hbClient->GetAuthToken();
    if (!token || lstrlen(token) == 0) {
        return;
    }

    // hb_conf.json lives on the device's flash, so it is only rewritten when
    // the token actually changed - re-authenticating against an unchanged
    // session should not cost a write.
    const TCHAR* storedToken = m_config->GetAuthToken();
    if (storedToken && lstrcmp(storedToken, token) == 0) {
        return;
    }

    m_config->SetAuthToken(token);

    const TCHAR* configPath = m_config->GetConfigPath();
    if (!configPath || !m_config->Save(configPath)) {
        // Not fatal: this session works, it just will not outlive the process.
        m_journal->LogError(TEXT("AUTH_SAVE_FAILED"),
                            TEXT("Auth token could not be written to hb_conf.json"));
    }
}

bool Controller::RetryWithFreshToken()
{
    if (!m_hbClient || !m_config) {
        return false;
    }

    // Only a 401 is worth a second attempt. A 404 for an unknown barcode, or a
    // call that never reached the server at all (status 0), must not cost the
    // operator another blocking round trip.
    if (m_hbClient->GetLastStatusCode() != kHttpUnauthorized) {
        return false;
    }

    // The stored token is the one that was just rejected, so drop it before
    // asking for a new one: leaving it in the configuration would have
    // EnsureAuthenticated restore it again on the next call and spend a wasted
    // request per scan re-discovering that it is dead.
    m_config->SetAuthToken(NULL);

    // Forced, so this always goes to the server rather than restoring anything.
    // The retry it authorises is a single repeat by the caller; a server that
    // keeps answering 401 fails that repeat and the call ends there.
    return EnsureAuthenticated(true);
}

bool Controller::LookupItem(const TCHAR* barcode, Models::Item* item)
{
    if (!m_hbClient || !barcode || !item) {
        return false;
    }

    if (!EnsureAuthenticated(false)) {
        return false;
    }

    if (m_hbClient->GetItem(barcode, item)) {
        return true;
    }

    // An expired session is the common failure after a shift-long gap in
    // coverage, or after a restart that restored a token the server has since
    // retired. That case, and only that case, is worth one extra round trip.
    if (!RetryWithFreshToken()) {
        return false;
    }
    return m_hbClient->GetItem(barcode, item);
}

void Controller::OnScanReceived(const TCHAR* barcode)
{
    if (!barcode || lstrlen(barcode) == 0 || m_busy) {
        return;
    }
    BusyScope busy(&m_busy);

    // Audit record only. This is not queue work: journaling it as a transaction
    // used to add a phantom entry to the offline queue for every single scan.
    m_journal->LogTransaction(TEXT("SCAN"), barcode, TEXT("Barcode scanned"));

    if (m_locationMode) {
        OnLocationScan(barcode);
        return;
    }

    SetState(STATE_SCANNING);

    Models::Item item;
    if (LookupItem(barcode, &item) && item.IsValid()) {
        m_journal->LogInfo(TEXT("Item lookup successful"));
        SetState(STATE_IDLE);
        ShowItemView(&item, NULL);
        return;
    }

    // The lookup did not resolve. Only a device that can actually reach the
    // server may conclude "no such item"; otherwise the scan is queued so it
    // survives to the next sync.
    if (m_syncEngine->IsOnline() && m_hbClient->IsAuthenticated()) {
        m_journal->LogInfo(TEXT("Item not found"));
        SetState(STATE_IDLE);

        if (IDYES == MessageBox(m_mainWindow,
                                TEXT("Item not found. Create it now?"),
                                TEXT("Not Found"),
                                MB_YESNO | MB_ICONQUESTION)) {
            ShowItemView(NULL, barcode);
        }
        return;
    }

    QueueScanForSync(barcode, NULL);
    SetState(STATE_IDLE);
}

void Controller::QueueScanForSync(const TCHAR* barcode, const TCHAR* locationId)
{
    if (!m_syncEngine || !barcode) {
        return;
    }

    // An installation that turns offline queueing off wants the operator to
    // know immediately that the work was not recorded, rather than to discover
    // a silently empty queue at the end of the shift.
    if (m_config && !m_config->IsOfflineModeEnabled()) {
        m_journal->LogError(TEXT("OFFLINE_DISABLED"),
                            TEXT("Scan discarded: offline queueing is disabled in hb_conf.json"));
        MessageBox(m_mainWindow,
                   TEXT("No connection, and offline queueing is disabled.\nThe scan was not saved."),
                   TEXT("Offline"),
                   MB_OK | MB_ICONERROR);
        return;
    }

    if (m_syncEngine->QueueScan(barcode, locationId)) {
        m_journal->LogInfo(TEXT("Scan queued for offline sync"));
        MessageBox(m_mainWindow,
                   TEXT("Device is offline. Scan queued for synchronization."),
                   TEXT("Offline Mode"),
                   MB_OK | MB_ICONWARNING);
    } else {
        m_journal->LogError(TEXT("QUEUE_FAILED"), TEXT("Failed to queue offline transaction"));
        MessageBox(m_mainWindow,
                   TEXT("Failed to queue transaction."),
                   TEXT("Error"),
                   MB_OK | MB_ICONERROR);
    }

    // The queue depth just changed; the title bar and the queue screen both
    // show it, and neither used to be told.
    RefreshQueueUI();
}

void Controller::ToggleLocationMode()
{
    m_locationMode = !m_locationMode;
    ClearPendingLocationScan();

    ShowScanView();
    if (m_scanView) {
        m_scanView->SetStatus(m_locationMode ? TEXT("Assign location: scan the item")
                                             : TEXT("Ready to scan"));
    }
}

void Controller::ClearPendingLocationScan()
{
    if (m_pendingItemBarcode) {
        delete[] m_pendingItemBarcode;
        m_pendingItemBarcode = NULL;
    }
}

void Controller::OnLocationScan(const TCHAR* barcode)
{
    // First scan names the item, second names the shelf it went onto.
    if (!m_pendingItemBarcode) {
        m_pendingItemBarcode = Str::Dup(barcode);
        if (m_scanView) {
            m_scanView->SetStatus(TEXT("Now scan the location"));
        }
        return;
    }

    bool pushed = false;
    if (EnsureAuthenticated(false)) {
        pushed = m_hbClient->UpdateItemLocation(m_pendingItemBarcode, barcode);
        if (!pushed && RetryWithFreshToken()) {
            pushed = m_hbClient->UpdateItemLocation(m_pendingItemBarcode, barcode);
        }
    }

    if (pushed) {
        m_journal->LogTransaction(TEXT("MOVE"), m_pendingItemBarcode, barcode);
        MessageBox(m_mainWindow, TEXT("Location updated."),
                   TEXT("Moved"), MB_OK | MB_ICONINFORMATION);
    } else {
        // Queued as "SCAN:<item>@<location>", which the sync engine replays as
        // a lookup plus an UpdateItemLocation once the device is back on line.
        QueueScanForSync(m_pendingItemBarcode, barcode);
    }

    ClearPendingLocationScan();
    m_locationMode = false;
    if (m_scanView) {
        m_scanView->SetStatus(TEXT("Ready to scan"));
    }
}

void Controller::OnSyncRequested()
{
    RunSync(true);
}

void Controller::RunSync(bool interactive)
{
    if (!m_syncEngine || m_busy || m_state == STATE_SYNCING) {
        return;
    }
    BusyScope busy(&m_busy);

    SetState(STATE_SYNCING);
    if (m_queueView) {
        m_queueView->UpdateSyncStatus(SyncEngine::SYNC_IN_PROGRESS);
    }

    // Replaying a queued transaction hits the API, so make sure there is a
    // session before the engine starts working through the backlog.
    EnsureAuthenticated(false);

    bool ok = m_syncEngine->Sync();

    // A stale token fails every entry in the batch, which looks exactly like a
    // server-side outage from here. Replaying under a fresh token is safe: the
    // engine only marks an entry synced once the server has accepted it, so the
    // second pass sends precisely what the first one could not.
    if (!ok && RetryWithFreshToken()) {
        ok = m_syncEngine->Sync();
    }

    if (ok) {
        m_journal->LogInfo(TEXT("Sync completed successfully"));
    } else {
        const TCHAR* error = m_syncEngine->GetLastSyncError();
        m_journal->LogError(TEXT("SYNC_FAILED"), error ? error : TEXT("Unknown sync failure"));

        // A background sync must never raise a modal dialog: the device is
        // typically in a holster and nobody would dismiss it.
        if (interactive) {
            MessageBox(m_mainWindow,
                       error ? error : TEXT("Synchronization failed."),
                       TEXT("Sync Failed"),
                       MB_OK | MB_ICONWARNING);
        }
    }

    SetState(STATE_IDLE);
    RefreshQueueUI();
}

void Controller::RefreshQueueUI()
{
    if (m_queueView && m_syncEngine) {
        m_queueView->RefreshQueue();
        m_queueView->UpdateSyncStatus(m_syncEngine->GetSyncStatus());
    }
    UpdateUI();
}

void Controller::OnConfigChanged()
{
    // Reload configuration
    m_config->LoadFromDefaultLocations();
    m_hbClient->SetBaseUrl(m_config->GetApiBaseUrl());

    // The base URL and credentials may both have moved, so the current session
    // is worthless; get a new one against the new server.
    EnsureAuthenticated(true);

    if (m_syncEngine) {
        m_syncEngine->SetAutoSyncIntervalSeconds(m_config->GetSyncIntervalSeconds());
        m_syncEngine->SetAutoSyncEnabled(m_config->GetSyncIntervalSeconds() > 0);
    }
}

bool Controller::InitializeUI()
{
    if (!CreateMainWindow()) {
        return false;
    }

    // Create the child views that make up the UI and wire their callbacks.
    if (!CreateViews()) {
        return false;
    }

    // The soft-key menu bar is a nicety; failure to create it is not fatal.
    CreateMenuBar();

    // The views were created before the menu bar shrank the client area, and
    // the WM_SIZE that would have told them about it arrived before they
    // existed, so size them once explicitly here.
    LayoutViews();

    // Start on the scan screen.
    ShowScanView();
    return true;
}

bool Controller::CreateViews()
{
    m_scanView = new Views::ScanView();
    m_queueView = new Views::QueueView();
    m_itemView = new Views::ItemView();

    if (!m_scanView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }
    if (!m_queueView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }
    if (!m_itemView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }

    // Scan pipeline: the hardware scanner and the on-screen Scan button both
    // deliver a barcode to the ScanView, which forwards it here via this thunk.
    m_scanView->SetScanner(m_scanner);
    m_scanView->SetScanCallback(&Controller::ScanCallbackThunk, this);

    // Queue view drives synchronization through the controller.
    m_queueView->SetSyncEngine(m_syncEngine);
    m_queueView->SetSyncCallback(&Controller::SyncCallbackThunk, this);
    m_queueView->SetQueueChangedCallback(&Controller::QueueChangedThunk, this);

    // Item editor saves flow back through the controller to the API client.
    m_itemView->SetSaveCallback(&Controller::ItemSaveThunk, this);
    m_itemView->SetCancelCallback(&Controller::ItemCancelThunk, this);

    // Only the scan view is visible initially.
    m_queueView->Show(false);
    m_itemView->Show(false);
    return true;
}

bool Controller::CreateMenuBar()
{
    // Create the Windows Mobile soft-key menu bar from the SHMENUBAR resource
    // (see resources/layout.rc). Menu selections arrive as WM_COMMAND on the
    // main window.
    SHMENUBARINFO mbi = {0};
    mbi.cbSize = sizeof(mbi);
    mbi.hwndParent = m_mainWindow;
    mbi.nToolBarId = IDR_MENUBAR;
    mbi.hInstRes = m_hInstance;
    mbi.dwFlags = SHCMBF_HIDESIPBUTTON;

    if (SHCreateMenuBar(&mbi)) {
        m_menuBar = mbi.hwndMB;
        return true;
    }

    // Losing the menu bar costs every navigation command, so record it: the
    // screen looks fine but Queue / Sync / Exit are simply unreachable.
    if (m_journal) {
        m_journal->LogError(TEXT("MENUBAR_FAILED"),
                            TEXT("SHCreateMenuBar failed; soft-key navigation unavailable"));
    }
    return false;
}

void Controller::LayoutViews()
{
    if (!m_mainWindow) {
        return;
    }

    RECT clientRect;
    GetClientRect(m_mainWindow, &clientRect);

    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    // Every view fills the client area; each one re-flows its own controls from
    // its WM_SIZE handler. Nothing used to resize them, so they kept the 240x320
    // they were created with while the real client area is roughly 240x268 once
    // the navigation bar and the soft-key menu bar have taken their strips -
    // which is what pushed the queue buttons off the bottom of the screen.
    if (m_scanView && m_scanView->GetHandle()) {
        MoveWindow(m_scanView->GetHandle(), 0, 0, width, height, TRUE);
    }
    if (m_queueView && m_queueView->GetHandle()) {
        MoveWindow(m_queueView->GetHandle(), 0, 0, width, height, TRUE);
    }
    if (m_itemView && m_itemView->GetHandle()) {
        MoveWindow(m_itemView->GetHandle(), 0, 0, width, height, TRUE);
    }
}

void Controller::ShowScanView()
{
    if (m_scanView) m_scanView->Show(true);
    if (m_queueView) m_queueView->Show(false);
    if (m_itemView) m_itemView->Show(false);
}

void Controller::ShowQueueView()
{
    if (m_queueView) {
        m_queueView->RefreshQueue();   // pull the latest pending transactions
        m_queueView->UpdateSyncStatus(m_syncEngine ? m_syncEngine->GetSyncStatus()
                                                   : SyncEngine::SYNC_IDLE);
        m_queueView->Show(true);
    }
    if (m_scanView) m_scanView->Show(false);
    if (m_itemView) m_itemView->Show(false);
}

void Controller::ShowItemView(const Models::Item* item, const TCHAR* newBarcode)
{
    if (!m_itemView) {
        return;
    }

    if (item) {
        m_itemView->DisplayItem(item);
    } else {
        m_itemView->DisplayNewItem(newBarcode);
    }

    m_itemView->SetEditable(true);
    m_itemView->Show(true);

    if (m_scanView) m_scanView->Show(false);
    if (m_queueView) m_queueView->Show(false);
}

// static
void Controller::ScanCallbackThunk(const TCHAR* barcode, void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->OnScanReceived(barcode);
    }
}

// static
void Controller::SyncCallbackThunk(void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->OnSyncRequested();
    }
}

// static
void Controller::ItemSaveThunk(const Models::Item* item, void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->OnItemSave(item);
    }
}

// static
void Controller::ItemCancelThunk(void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->ShowScanView();
    }
}

// static
void Controller::QueueChangedThunk(void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->UpdateUI();
    }
}

void Controller::OnItemSave(const Models::Item* item)
{
    if (!item || !m_hbClient || m_busy) {
        return;
    }
    BusyScope busy(&m_busy);

    // Existing item (has an id) -> update; otherwise create.
    bool hasId = (item->GetId() != NULL && lstrlen(item->GetId()) > 0);

    bool ok = false;
    if (EnsureAuthenticated(false)) {
        ok = hasId ? m_hbClient->UpdateItem(item) : m_hbClient->CreateItem(item);

        // A 401 means the server never applied the edit, so repeating it under a
        // new token cannot duplicate the item.
        if (!ok && RetryWithFreshToken()) {
            ok = hasId ? m_hbClient->UpdateItem(item) : m_hbClient->CreateItem(item);
        }
    }

    if (ok) {
        m_journal->LogInfo(TEXT("Item saved to server"));
        m_journal->LogTransaction(hasId ? TEXT("ITEM_UPDATE") : TEXT("ITEM_CREATE"),
                                  item->GetBarcode() ? item->GetBarcode() : TEXT(""),
                                  TEXT("Saved from item editor"));
        MessageBox(m_mainWindow, TEXT("Item saved."), TEXT("Saved"), MB_OK | MB_ICONINFORMATION);
        ShowScanView();
        return;
    }

    // Could not reach the server: queue the edit so the work is not lost. The
    // payload carries the whole item as JSON, which has no bounded length, so
    // it is assembled in a growable buffer rather than formatted into a fixed
    // one that wsprintf would silently cut at 1024 characters.
    TCHAR* json = item->ToJson();
    bool queued = false;
    if (json) {
        Str::Buffer payload;
        payload.Append(TEXT("UPDATE:"));
        payload.Append(json);
        delete[] json;

        if (!payload.Failed()) {
            queued = m_syncEngine && m_syncEngine->QueueTransaction(TEXT("ITEM_UPDATE"),
                                                                    payload.Get());
        }
    }

    if (queued) {
        m_journal->LogInfo(TEXT("Item update queued for sync"));
        MessageBox(m_mainWindow, TEXT("Offline - item queued for sync."),
                   TEXT("Offline Mode"), MB_OK | MB_ICONWARNING);
        RefreshQueueUI();
        ShowScanView();
    } else {
        m_journal->LogError(TEXT("ITEM_SAVE_FAILED"), TEXT("Item could not be saved or queued"));
        MessageBox(m_mainWindow, TEXT("Failed to save item."),
                   TEXT("Error"), MB_OK | MB_ICONERROR);
    }
}

void Controller::UpdateUI()
{
    if (!m_mainWindow) {
        return;
    }

    // Update window title based on state. Built with the bounded helpers rather
    // than wsprintf so a future variable-length field cannot overrun it.
    const int kTitleCap = 128;
    TCHAR title[kTitleCap];
    Str::Copy(title, kTitleCap, TEXT("HomeBox Client"));

    switch (m_state) {
    case STATE_INIT:
        Str::Append(title, kTitleCap, TEXT(" - Initializing..."));
        break;

    case STATE_IDLE:
        // The connection state matters more than anything else on this screen:
        // an unauthenticated device silently queues everything it scans.
        Str::Append(title, kTitleCap,
                    (m_hbClient && m_hbClient->IsAuthenticated()) ? TEXT(" - Ready")
                                                                  : TEXT(" - Offline"));
        Str::Append(title, kTitleCap, TEXT(" [Queue: "));
        Str::AppendInt(title, kTitleCap,
                       m_syncEngine ? m_syncEngine->GetQueuedTransactionCount() : 0);
        Str::Append(title, kTitleCap, TEXT("]"));
        break;

    case STATE_SCANNING:
        Str::Append(title, kTitleCap, TEXT(" - Scanning..."));
        break;

    case STATE_SYNCING:
        Str::Append(title, kTitleCap, TEXT(" - Syncing..."));
        break;

    case STATE_ERROR:
        Str::Append(title, kTitleCap, TEXT(" - Error"));
        break;

    default:
        break;
    }

    SetWindowText(m_mainWindow, title);

    // Force window redraw
    InvalidateRect(m_mainWindow, NULL, TRUE);
    UpdateWindow(m_mainWindow);
}

bool Controller::CreateMainWindow()
{
    // Register window class
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kMainWindowClass;

    if (!RegisterClass(&wc)) {
        return false;
    }

    // Create main window
    m_mainWindow = CreateWindow(
        kMainWindowClass,
        TEXT("HomeBox Client"),
        WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
        m_hInstance,
        this  // Pass 'this' pointer for use in WindowProc
    );

    return (m_mainWindow != NULL);
}

LRESULT CALLBACK Controller::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Controller* pController = NULL;

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pController = (Controller*)pCreate->lpCreateParams;
        SetWindowLong(hwnd, GWL_USERDATA, (LONG)pController);
    } else {
        pController = (Controller*)GetWindowLong(hwnd, GWL_USERDATA);
    }

    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (pController) {
            // Confirm exit
            if (IDYES == MessageBox(hwnd, TEXT("Exit application?"), TEXT("Confirm"), MB_YESNO)) {
                DestroyWindow(hwnd);
            }
        }
        return 0;

    case WM_SIZE:
        // The menu bar and the SIP both change the client area after the views
        // have been created, so re-fill it every time it moves.
        if (pController) {
            pController->LayoutViews();
            return 0;
        }
        break;

    case WM_SETTINGCHANGE:
        // Raising or lowering the input panel changes the usable client area
        // without necessarily resizing the frame.
        if (pController) {
            pController->LayoutViews();
        }
        break;

    case WM_TIMER:
        if (pController && wParam == (WPARAM)TIMER_AUTOSYNC) {
            pController->OnAutoSyncTick();
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (pController) {
            pController->OnActivate(LOWORD(wParam) != WA_INACTIVE);
        }
        break;

    case WM_HIBERNATE:
        // Low-memory warning: drop the queue list's cached rows. ShowQueueView
        // rebuilds them from the journal the next time the user looks.
        if (pController && pController->m_queueView) {
            pController->m_queueView->ClearQueue();
            return 0;
        }
        break;

    case WM_COMMAND:
        // Soft-key menu selections (the child views handle their own control
        // commands via their own window procedures).
        if (pController) {
            switch (LOWORD(wParam)) {
            case IDM_VIEW_SCAN:
                pController->ShowScanView();
                return 0;
            case IDM_VIEW_QUEUE:
                pController->ShowQueueView();
                return 0;
            case IDM_VIEW_ITEM:
                pController->ShowItemView(NULL, NULL);
                return 0;
            case IDM_ACTION_SYNC:
                pController->OnSyncRequested();
                return 0;
            case IDM_ACTION_SETLOC:
                pController->ToggleLocationMode();
                return 0;
            case IDM_HELP_ABOUT:
                MessageBox(hwnd, TEXT("HomeBox Client\nVersion 1.0.0"),
                           TEXT("About"), MB_OK | MB_ICONINFORMATION);
                return 0;
            case IDM_FILE_EXIT:
                DestroyWindow(hwnd);
                return 0;
            }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    default:
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace HBX
