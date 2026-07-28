#include "../include/Controller.hpp"
#include "../include/NbClient.hpp"
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

// Destination keys, as stored in the picker history and parsed back here. The
// key is normalised so a rack reached by scanning a printed label, by typing an
// id and by picking it out of the history all produce the same history entry.
const TCHAR* const kRackKeyPrefix = TEXT("rack:");
const TCHAR* const kSiteKeyPrefix = TEXT("site:");
const TCHAR* const kLocKeyPrefix  = TEXT("loc:");

/** Sentinel destination meaning "take it out of the rack, leave it where it is". */
const TCHAR* const kUnrackKey = TEXT("unrack");

/**
 * True when `s` is a non-empty run of digits. NetBox object ids are small
 * positive integers, so this is the whole of the validation a typed id needs
 * before it is put in a URL.
 */
bool IsPositiveInteger(const TCHAR* s)
{
    if (!s || s[0] == 0) {
        return false;
    }
    for (int i = 0; s[i] != 0; i++) {
        if (s[i] < (TCHAR)'0' || s[i] > (TCHAR)'9') {
            return false;
        }
    }
    return true;
}

/**
 * True when `s` is a decimal number. A rack position is not an integer: NetBox
 * models half-U slots as 42.5, and parsing one as an int truncates it silently
 * into the wrong slot.
 */
bool IsDecimalNumber(const TCHAR* s)
{
    if (!s || s[0] == 0) {
        return false;
    }

    int digits = 0;
    int dots = 0;
    for (int i = 0; s[i] != 0; i++) {
        if (s[i] >= (TCHAR)'0' && s[i] <= (TCHAR)'9') {
            digits++;
        } else if (s[i] == (TCHAR)'.') {
            dots++;
        } else {
            return false;
        }
    }
    return digits > 0 && dots <= 1;
}

/** Compares the first `len` characters of `s` with `prefix`. */
bool StartsWith(const TCHAR* s, const TCHAR* prefix)
{
    if (!s || !prefix) {
        return false;
    }
    int i = 0;
    while (prefix[i] != 0) {
        if (s[i] != prefix[i]) {
            return false;
        }
        i++;
    }
    return true;
}

/**
 * Extracts the id that follows `marker` in a NetBox object URL, e.g.
 * "/dcim/racks/12/". Some label plugins print the object's web URL as a QR
 * code, and honouring it costs nothing while meeting those sites where they
 * already are. Returns false when the marker is absent or is not followed by
 * digits.
 */
bool IdAfterMarker(const TCHAR* text, const TCHAR* marker, TCHAR* out, int cap)
{
    const TCHAR* hit = wcsstr(text, marker);
    if (!hit) {
        return false;
    }

    const TCHAR* digits = hit + lstrlen(marker);
    int len = 0;
    while (digits[len] >= (TCHAR)'0' && digits[len] <= (TCHAR)'9') {
        len++;
    }
    if (len == 0) {
        return false;
    }
    return Str::CopyN(out, cap, digits, len);
}

/** NULL for a field the move does not touch; the value for one it does. */
const TCHAR* Optional(bool present, const TCHAR* value)
{
    return present ? value : NULL;
}

} // namespace

Controller::Controller()
    : m_hInstance(NULL)
    , m_mainWindow(NULL)
    , m_state(STATE_INIT)
    , m_config(NULL)
    , m_hbClient(NULL)
    , m_nbClient(NULL)
    , m_syncEngine(NULL)
    , m_journal(NULL)
    , m_scanner(NULL)
    , m_scanView(NULL)
    , m_queueView(NULL)
    , m_itemView(NULL)
    , m_deviceView(NULL)
    , m_pickerView(NULL)
    , m_menuBar(NULL)
    , m_activeView(VIEW_SCAN)
    , m_pickerPurpose(PICK_NONE)
    , m_secondInstance(false)
    , m_instanceMutex(NULL)
    , m_autoSyncTimerRunning(false)
    , m_busy(false)
    , m_locationMode(false)
    , m_pendingItemBarcode(NULL)
{
    m_targetId[0] = 0;
    m_targetTitle[0] = 0;
    m_targetSiteId[0] = 0;
    m_targetLocationId[0] = 0;
    CancelPendingMove();
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

    // Configure the API clients and decide which one takes new work. The
    // timeout matters as much as the URL here: the transport's own default
    // would block a scan handler for half a minute on a link that has gone away.
    ConfigureBackends();

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
    if (m_deviceView) {
        delete m_deviceView;
        m_deviceView = NULL;
    }
    if (m_pickerView) {
        delete m_pickerView;
        m_pickerView = NULL;
    }

    // Cleanup components. The sync engine holds non-owning pointers to every
    // registered backend, so it goes first.
    if (m_syncEngine) {
        delete m_syncEngine;
        m_syncEngine = NULL;
    }

    if (m_hbClient) {
        delete m_hbClient;
        m_hbClient = NULL;
    }

    if (m_nbClient) {
        delete m_nbClient;
        m_nbClient = NULL;
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

InventoryBackend* Controller::GetActiveBackend()
{
    return m_syncEngine ? m_syncEngine->GetActiveBackend() : NULL;
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

void Controller::ConfigureBackends()
{
    if (!m_config || !m_syncEngine || !m_journal || !m_hbClient) {
        return;
    }

    m_hbClient->SetBaseUrl(m_config->GetApiBaseUrl());
    m_hbClient->SetRequestTimeout((DWORD)REQUEST_TIMEOUT_MS);

    // The registry keys on the id a backend reports and that id is written into
    // every queue record, so the configured one has to reach the client before
    // anything is queued. An id that cannot survive a queue record is refused
    // rather than applied: registration would then drop the backend altogether
    // and the device would stop queueing anything at all.
    const TCHAR* homeboxId = m_config->GetHomeboxInstanceId();
    if (SyncEngine::IsValidInstanceId(homeboxId)) {
        m_hbClient->SetInstanceId(homeboxId);
    } else {
        m_journal->LogError(TEXT("BACKEND_ID_INVALID"),
                            TEXT("homeboxInstanceId is unusable in a queue record; keeping the default"));
    }

    // Registration is idempotent, so this also covers a configuration reload.
    m_syncEngine->RegisterBackend(m_hbClient);

    ConfigureNetbox();

    // Exactly one backend takes new work. An activeBackend that names something
    // which is not configured must not quietly send scans to another server, so
    // fall back to HomeBox and leave a record of why.
    if (!m_syncEngine->SetActiveBackend(m_config->GetActiveBackendId())) {
        m_syncEngine->SetActiveBackend(m_hbClient->GetInstanceId());
        m_journal->LogError(TEXT("BACKEND_UNKNOWN"),
                            TEXT("activeBackend in hb_conf.json is not configured; using HomeBox"));
    }
}

void Controller::ConfigureNetbox()
{
    if (!m_config || !m_syncEngine || !m_journal) {
        return;
    }

    const TCHAR* baseUrl = m_config->GetNetboxBaseUrl();
    const bool configured = (baseUrl && baseUrl[0] != 0);

    if (!m_nbClient) {
        // An unconfigured NetBox is never built and never registered. A
        // registered backend with no URL could be selected by activeBackend,
        // and every scan would then resolve against nothing while the device
        // looked perfectly healthy.
        if (!configured) {
            return;
        }
        m_nbClient = new NbClient();
    }

    // The registry keys on the id the backend reports and that id is written
    // into every queue record, so it has to be settled before registration. An
    // id that cannot survive a queue record is refused rather than applied.
    const TCHAR* netboxId = m_config->GetNetboxInstanceId();
    if (SyncEngine::IsValidInstanceId(netboxId)) {
        m_nbClient->SetInstanceId(netboxId);
    } else {
        m_journal->LogError(TEXT("BACKEND_ID_INVALID"),
                            TEXT("netboxInstanceId is unusable in a queue record; keeping the default"));
    }

    m_nbClient->SetBaseUrl(configured ? baseUrl : TEXT(""));
    m_nbClient->SetRequestTimeout((DWORD)REQUEST_TIMEOUT_MS);
    m_nbClient->SetAuth(m_config->GetNetboxAuthScheme(), m_config->GetNetboxToken());

    // Windows Mobile 6.5 tops out at TLS 1.0 and cannot validate a modern
    // certificate at all, so a site may have to turn validation off to reach a
    // local NetBox over HTTPS. That is a deliberate downgrade and belongs in
    // the audit trail rather than only in a config file nobody re-reads.
    const bool insecure = m_config->IsInsecureTlsAllowed();
    m_nbClient->SetIgnoreCertificateErrors(insecure);
    if (insecure) {
        m_journal->LogInfo(TEXT("NetBox TLS certificate validation is disabled by configuration"));
    }

    // Registration is idempotent. A NetBox whose URL is later removed stays
    // registered on purpose: entries already queued for it can only replay
    // while their backend is known, and stranding them silently would be worse
    // than leaving them visible in the queue view.
    m_syncEngine->RegisterBackend(m_nbClient);
}

NbClient* Controller::ActiveNetbox() const
{
    if (!m_nbClient || !m_syncEngine) {
        return NULL;
    }
    return (m_syncEngine->GetActiveBackend() == (InventoryBackend*)m_nbClient) ? m_nbClient : NULL;
}

bool Controller::EnsureAuthenticated(bool force)
{
    InventoryBackend* backend = GetActiveBackend();
    if (!backend || !m_hbClient || !m_config) {
        return false;
    }

    if (!force && backend->IsAuthenticated()) {
        return true;
    }

    // Only HomeBox exchanges credentials for a session. Every other backend is
    // configured with a long-lived token and either holds one or does not,
    // which is exactly what its own Authenticate() reports - there is no
    // handshake to run and nothing to persist afterwards.
    if (backend != (InventoryBackend*)m_hbClient) {
        if (backend->Authenticate()) {
            return true;
        }
        m_journal->LogError(TEXT("AUTH_NO_TOKEN"),
                            TEXT("The active backend has no usable API token; check hb_conf.json"));
        return false;
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
    InventoryBackend* backend = GetActiveBackend();
    if (!backend || !m_config) {
        return false;
    }

    // A session that cannot be renewed must not be retried. A NetBox API token
    // is static configuration, so a 401 there means the configured token is
    // wrong or revoked: re-authenticating cannot change that and would spend a
    // blocking round trip on every single scan discovering it again.
    if (!backend->SessionIsRenewable()) {
        return false;
    }

    // Only a 401 is worth a second attempt. A 404 for an unknown barcode, or a
    // call that never reached the server at all (status 0), must not cost the
    // operator another blocking round trip.
    if (backend->GetLastStatusCode() != kHttpUnauthorized) {
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

    // Audit record only. This is not queue work: journaling it as a transaction
    // used to add a phantom entry to the offline queue for every single scan.
    m_journal->LogTransaction(TEXT("SCAN"), barcode, TEXT("Barcode scanned"));

    // Route the decode to the screen that is up. The scanner has a single sink
    // and it is still ScanView's - the EMDK thread hand-off is unchanged - but
    // a trigger pull while a picker is waiting for a rack answers that picker
    // rather than starting a fresh lookup that would throw the move away.
    if (m_activeView == VIEW_PICKER && m_pickerView) {
        if (m_pickerView->SelectByCode(barcode)) {
            return;
        }

        // A label that matches no row is only meaningful where scanning one is
        // the intended way to answer - a printed rack, site or location tag.
        // Anywhere else a stray decode would be taken for a typed value, and
        // for the status picker that means PATCHing a barcode into the status
        // field, which the client deliberately does not validate.
        if (m_pickerPurpose == PICK_MOVE_DEST) {
            OnPickerChoice(barcode, false);
        } else {
            m_pickerView->SetMessage(TEXT("That scan is not one of these choices"));
        }
        return;
    }

    BusyScope busy(&m_busy);

    if (m_locationMode) {
        OnLocationScan(barcode);
        return;
    }

    SetState(STATE_SCANNING);
    OnAssetScan(barcode);
}

void Controller::OnAssetScan(const TCHAR* code)
{
    InventoryBackend* backend = GetActiveBackend();
    if (!backend) {
        SetState(STATE_IDLE);
        MessageBox(m_mainWindow,
                   TEXT("No inventory system is configured."),
                   TEXT("Not Configured"), MB_OK | MB_ICONERROR);
        return;
    }

    // HomeBox keeps the path it has always had. ItemView is an editor over
    // Models::Item and it is where creating and editing an item lives, so
    // routing HomeBox scans through the read-only detail screen would remove a
    // working feature. The branch is on "the backend this class holds an editor
    // for", not on a backend name - everything else takes the neutral path.
    if (backend == (InventoryBackend*)m_hbClient) {
        Models::Item item;
        if (LookupItem(code, &item) && item.IsValid()) {
            m_journal->LogInfo(TEXT("Item lookup successful"));
            SetState(STATE_IDLE);
            ShowItemView(&item, NULL);
            return;
        }

        // Only a device that can actually reach the server may conclude "no
        // such item"; otherwise the scan is queued so it survives to the next
        // sync. ITEM_SCAN is HomeBox's own replay grammar, which is why this
        // lives on this side of the branch.
        if (m_syncEngine->IsOnline() && backend->IsAuthenticated()) {
            m_journal->LogInfo(TEXT("Item not found"));
            SetState(STATE_IDLE);

            if (IDYES == MessageBox(m_mainWindow,
                                    TEXT("Item not found. Create it now?"),
                                    TEXT("Not Found"),
                                    MB_YESNO | MB_ICONQUESTION)) {
                ShowItemView(NULL, code);
            }
            return;
        }

        QueueScanForSync(code, NULL);
        SetState(STATE_IDLE);
        return;
    }

    Models::AssetSummary summary;
    bool found = false;
    bool lookupRan = false;

    if (EnsureAuthenticated(false)) {
        lookupRan = true;
        found = backend->LookupByCode(code, &summary);

        // Gated inside RetryWithFreshToken on SessionIsRenewable, so a NetBox
        // 401 costs nothing here.
        if (!found && RetryWithFreshToken()) {
            found = backend->LookupByCode(code, &summary);
        }
    }

    SetState(STATE_IDLE);

    if (found) {
        m_journal->LogInfo(TEXT("Asset lookup successful"));

        // A successful LookupByCode means exactly one thing matched, so the
        // record behind it is match 0.
        ShowDeviceView(&summary, 0);
        return;
    }

    // More than one match is not a failure, it is a question. The match count
    // is only meaningful when a lookup actually ran; consulting it otherwise
    // would offer the results of the previous scan.
    const int matches = lookupRan ? backend->GetMatchCount() : 0;
    if (matches > 1) {
        ShowMatchPicker(backend, matches);
        return;
    }

    if (!lookupRan) {
        ReportBackendFailure(TEXT("Could not start a session with this system."));
        return;
    }
    if (backend->GetLastStatusCode() == kHttpUnauthorized) {
        ReportBackendFailure(TEXT("The server rejected this lookup."));
        return;
    }

    if (!m_syncEngine->IsOnline()) {
        // A lookup is deliberately never queued on this path. Replaying one
        // hours later produces a record nobody is looking at, and a code that
        // does not exist on the server would fail forever, pinning the sync
        // status and making the queue screen permanently red.
        MessageBox(m_mainWindow,
                   TEXT("Offline - cannot look this up.\nReconnect and scan it again."),
                   TEXT("Offline"), MB_OK | MB_ICONWARNING);
        return;
    }

    m_journal->LogInfo(TEXT("Asset not found"));
    MessageBox(m_mainWindow,
               TEXT("Nothing in this system matches that code."),
               TEXT("Not Found"), MB_OK | MB_ICONINFORMATION);
}

void Controller::ShowMatchPicker(InventoryBackend* backend, int matchCount)
{
    if (!backend || !m_pickerView) {
        return;
    }

    const int shown = (matchCount < (int)Views::PickerView::MAX_CHOICES)
                          ? matchCount : (int)Views::PickerView::MAX_CHOICES;

    // A backend can hold back matches it did not retain. Saying "5 matches"
    // when the server found twelve invites the operator to conclude their
    // device is not in NetBox, or worse to pick the closest-looking row - so
    // the count on screen is the server's, and the shortfall is visible.
    int total = matchCount;
    if (m_nbClient && backend == (InventoryBackend*)m_nbClient) {
        const int reported = m_nbClient->GetTotalMatchCount();
        if (reported > total) {
            total = reported;
        }
    }

    TCHAR prompt[MESSAGE_CHARS];
    prompt[0] = 0;
    if (total > shown) {
        Str::AppendInt(prompt, MESSAGE_CHARS, (long)shown);
        Str::Append(prompt, MESSAGE_CHARS, TEXT(" of "));
        Str::AppendInt(prompt, MESSAGE_CHARS, (long)total);
        Str::Append(prompt, MESSAGE_CHARS, TEXT(" matches"));
    } else {
        Str::AppendInt(prompt, MESSAGE_CHARS, (long)total);
        Str::Append(prompt, MESSAGE_CHARS, TEXT(" matches - pick one"));
    }

    // No history for this question: the ids never repeat, and the server's
    // order carries meaning that reordering would destroy.
    m_pickerView->BeginChoices(prompt, NULL);

    for (int i = 0; i < matchCount; i++) {
        Models::AssetSummary match;
        if (!backend->GetMatch(i, &match)) {
            continue;
        }

        // The row has to carry enough to tell two identical chassis apart, so
        // it names where the asset is and not only what it is called. Serials
        // are not unique in NetBox: "which db-0x is this?" is exactly the
        // question being asked.
        TCHAR where[Views::PickerView::DETAIL_MAX];
        where[0] = 0;

        const TCHAR* site = match.FindValue(TEXT("Site"));
        const TCHAR* rack = match.FindValue(TEXT("Rack"));
        const TCHAR* position = match.FindValue(TEXT("Position"));

        if (site && site[0] != 0) {
            Str::Append(where, Views::PickerView::DETAIL_MAX, site);
        }
        if (rack && rack[0] != 0) {
            if (where[0] != 0) {
                Str::Append(where, Views::PickerView::DETAIL_MAX, TEXT(" "));
            }
            Str::Append(where, Views::PickerView::DETAIL_MAX, rack);
        }
        if (position && position[0] != 0) {
            if (where[0] != 0) {
                Str::Append(where, Views::PickerView::DETAIL_MAX, TEXT(" "));
            }
            Str::Append(where, Views::PickerView::DETAIL_MAX, position);
        }
        if (where[0] == 0) {
            // Whatever the backend put in the subtitle is a better answer than
            // a blank column, and it keeps this readable for a backend whose
            // summaries use different labels.
            Str::Copy(where, Views::PickerView::DETAIL_MAX, match.GetSubtitle());
        }

        const TCHAR* title = match.GetTitle();
        if (!title || title[0] == 0) {
            title = match.GetCode();
        }

        TCHAR key[16];
        key[0] = 0;
        Str::AppendInt(key, 16, (long)i);

        if (!m_pickerView->AddChoice(key, title, where, NULL)) {
            break;   // the list is full; the prompt already says so
        }
    }

    m_pickerView->SetInputHint((total > shown) ? TEXT("Not here? Scan the asset tag")
                                               : TEXT("Press the row number"));
    m_pickerView->EndChoices();
    ShowPicker(PICK_MATCH);
}

void Controller::ReportBackendFailure(const TCHAR* what)
{
    InventoryBackend* backend = GetActiveBackend();
    const int status = backend ? backend->GetLastStatusCode() : 0;

    TCHAR message[MESSAGE_CHARS];
    message[0] = 0;
    Str::Append(message, MESSAGE_CHARS,
                what ? what : TEXT("The server would not accept the request."));

    if (status == kHttpUnauthorized) {
        // A backend whose session cannot be renewed was configured with a
        // long-lived token, so a 401 is a configuration fault rather than an
        // expired session. Telling the operator to wait for a retry would be
        // wrong, and retrying is what the controller has already declined to do.
        Str::Append(message, MESSAGE_CHARS,
                    (backend && !backend->SessionIsRenewable())
                        ? TEXT("\nCheck the API token in hb_conf.json.")
                        : TEXT("\nThe session was rejected."));
    } else if (status > 0) {
        Str::Append(message, MESSAGE_CHARS, TEXT("\nServer status "));
        Str::AppendInt(message, MESSAGE_CHARS, (long)status);
    } else {
        Str::Append(message, MESSAGE_CHARS, TEXT("\nThe server could not be reached."));
    }

    m_journal->LogError(TEXT("BACKEND_REFUSED"), message);
    MessageBox(m_mainWindow, message, TEXT("Server"), MB_OK | MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// Detail screen actions
// ---------------------------------------------------------------------------

void Controller::OnDeviceAction(int action)
{
    switch (action) {
    case Views::DeviceView::ACTION_MOVE:
        BeginMove();
        break;
    case Views::DeviceView::ACTION_STATUS:
        BeginStatusChange();
        break;
    case Views::DeviceView::ACTION_BACK:
    default:
        ShowScanView();
        break;
    }
}

bool Controller::CaptureActionTarget()
{
    const Models::AssetSummary* asset = m_deviceView ? m_deviceView->GetAsset() : NULL;
    if (!asset || asset->GetId()[0] == 0) {
        if (m_deviceView) {
            m_deviceView->SetNotice(TEXT("This record has no id to write back to"));
        }
        return false;
    }

    Str::Copy(m_targetId, TARGET_ID_CHARS, asset->GetId());

    const TCHAR* title = asset->GetTitle();
    if (!title || title[0] == 0) {
        title = asset->GetCode();
    }
    Str::Copy(m_targetTitle, TARGET_TITLE_CHARS, title);
    return true;
}

void Controller::ReturnToDevice(const TCHAR* notice)
{
    m_pickerPurpose = PICK_NONE;

    if (m_deviceView && m_deviceView->GetAsset()) {
        m_deviceView->SetNotice(notice);
        ShowOnly(VIEW_DEVICE);
    } else {
        ShowScanView();
        if (m_scanView && notice && notice[0] != 0) {
            m_scanView->SetStatus(notice);
        }
    }

    RefreshQueueUI();
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void Controller::BeginStatusChange()
{
    InventoryBackend* backend = GetActiveBackend();
    if (!backend || !backend->SupportsStatus() || !m_pickerView) {
        return;
    }
    if (!CaptureActionTarget()) {
        return;
    }

    m_pickerView->BeginChoices(TEXT("New status"), TEXT("status"));

    // NetBox's status list is user-extensible, so a deployment can have more
    // values than there are keypad digits. The picker keeps the most recently
    // used ones at the top, which is the right nine for an operator who works
    // the same two or three transitions all shift.
    const int count = backend->GetStatusChoiceCount();
    for (int i = 0; i < count; i++) {
        const TCHAR* value = backend->GetStatusChoice(i);
        if (!value || value[0] == 0) {
            continue;
        }
        if (!m_pickerView->AddChoice(value, value, TEXT(""), NULL)) {
            break;
        }
    }

    m_pickerView->SetInputHint(TEXT("Press the row number"));
    m_pickerView->EndChoices();
    ShowPicker(PICK_STATUS);
}

void Controller::ApplyStatus(const TCHAR* statusValue)
{
    // A sync running on the shared transport means this cannot go now. Landing
    // back on the detail screen matters as much as declining: the picker has
    // already been told its question is over, so returning silently would leave
    // a screen with nothing left to answer.
    if (!statusValue || statusValue[0] == 0 || m_targetId[0] == 0 || m_busy) {
        ReturnToDevice(TEXT("Busy - try again in a moment"));
        return;
    }
    BusyScope busy(&m_busy);

    // The neutral interface advertises that a backend has statuses but carries
    // no way to write one, so the write goes through the typed client. There is
    // exactly one implementation with a status field today.
    NbClient* netbox = ActiveNetbox();

    bool sent = false;
    if (netbox && EnsureAuthenticated(false)) {
        sent = netbox->SetDeviceStatus(m_targetId, statusValue);
        if (!sent && RetryWithFreshToken()) {
            sent = netbox->SetDeviceStatus(m_targetId, statusValue);
        }
    }

    if (sent) {
        m_journal->LogTransaction(TEXT("DEVICE_STATUS"), m_targetId, statusValue);

        TCHAR notice[MESSAGE_CHARS];
        notice[0] = 0;
        Str::Append(notice, MESSAGE_CHARS, TEXT("Status set to "));
        Str::Append(notice, MESSAGE_CHARS, statusValue);
        ReturnToDevice(notice);
        return;
    }

    // Could not reach the server, so record the intent. This is safe to replay:
    // it names an object id the device learned while it was online, so it can
    // only ever apply to the device the operator was standing in front of.
    Str::Buffer payload;
    payload.Append(TEXT("STATUS:"));
    payload.Append(m_targetId);
    payload.AppendChar((TCHAR)':');
    payload.Append(statusValue);

    bool queued = false;
    if (!payload.Failed() && m_syncEngine) {
        queued = m_syncEngine->QueueTransaction(TEXT("DEVICE_STATUS"), payload.Get());
    }

    if (queued) {
        m_journal->LogInfo(TEXT("Status change queued for sync"));
        ReturnToDevice(TEXT("Offline - status change queued"));
    } else {
        m_journal->LogError(TEXT("STATUS_QUEUE_FAILED"),
                            TEXT("Status change could not be sent or queued"));
        ReturnToDevice(TEXT("Status change was not saved"));
    }
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------

void Controller::CancelPendingMove()
{
    m_move.site[0] = 0;
    m_move.location[0] = 0;
    m_move.rack[0] = 0;
    m_move.position[0] = 0;
    m_move.face[0] = 0;

    m_move.hasSite = false;
    m_move.hasLocation = false;
    m_move.hasRack = false;
    m_move.hasPosition = false;
    m_move.hasFace = false;
}

void Controller::BeginMove()
{
    if (!ActiveNetbox()) {
        if (m_deviceView) {
            m_deviceView->SetNotice(TEXT("Moving is not available for this system"));
        }
        return;
    }
    if (!CaptureActionTarget()) {
        return;
    }

    CancelPendingMove();
    AskMoveDestination();
}

void Controller::AskMoveDestination()
{
    if (!m_pickerView) {
        return;
    }

    TCHAR prompt[MESSAGE_CHARS];
    prompt[0] = 0;
    Str::Append(prompt, MESSAGE_CHARS, TEXT("Move "));
    Str::Append(prompt, MESSAGE_CHARS, m_targetTitle);
    Str::Append(prompt, MESSAGE_CHARS, TEXT(" to"));

    m_pickerView->BeginChoices(prompt, TEXT("dest"));

    // Taking a device out of a rack is a destination like any other, and it is
    // the one that has no label to scan.
    m_pickerView->AddChoice(kUnrackKey, TEXT("Unrack"), TEXT("out of the rack"), NULL);

    // Everything else is supplied by the history: a printed rack label scans
    // straight through, and the racks this operator has already worked in today
    // are the rows.
    m_pickerView->SetInputHint(TEXT("Scan a rack label, or type a rack id"));
    m_pickerView->EndChoices();
    ShowPicker(PICK_MOVE_DEST);
}

void Controller::AskMovePosition()
{
    if (!m_pickerView) {
        return;
    }

    m_pickerView->BeginChoices(TEXT("Rack position"), TEXT("pos"));
    m_pickerView->SetInputHint(TEXT("Type the U, e.g. 12 or 42.5"));
    m_pickerView->EndChoices();
    ShowPicker(PICK_MOVE_POSITION);
}

void Controller::AskMoveFace()
{
    if (!m_pickerView) {
        return;
    }

    m_pickerView->BeginChoices(TEXT("Rack face"), TEXT("face"));
    m_pickerView->AddChoice(TEXT("front"), TEXT("Front"), TEXT(""), NULL);
    m_pickerView->AddChoice(TEXT("rear"), TEXT("Rear"), TEXT(""), NULL);
    m_pickerView->SetInputHint(TEXT("1 front, 2 rear"));
    m_pickerView->EndChoices();
    ShowPicker(PICK_MOVE_FACE);
}

bool Controller::ApplyDestinationToken(const TCHAR* token)
{
    if (!token || token[0] == 0) {
        return false;
    }

    TCHAR id[MoveRequest::VALUE_CHARS];
    id[0] = 0;

    // A printed label carries the NetBox integer id rather than the name:
    // names get renamed, ids do not, and no round trip is needed to read one.
    if (StartsWith(token, TEXT("NBRACK:")) || StartsWith(token, kRackKeyPrefix)) {
        Str::Copy(id, MoveRequest::VALUE_CHARS, token + (StartsWith(token, TEXT("NBRACK:")) ? 7 : 5));
    } else if (StartsWith(token, TEXT("NBSITE:")) || StartsWith(token, kSiteKeyPrefix)) {
        Str::Copy(id, MoveRequest::VALUE_CHARS, token + (StartsWith(token, TEXT("NBSITE:")) ? 7 : 5));
        if (!IsPositiveInteger(id)) {
            return false;
        }
        Str::Copy(m_move.site, MoveRequest::VALUE_CHARS, id);
        m_move.hasSite = true;
        return true;
    } else if (StartsWith(token, TEXT("NBLOC:")) || StartsWith(token, kLocKeyPrefix)) {
        Str::Copy(id, MoveRequest::VALUE_CHARS, token + (StartsWith(token, TEXT("NBLOC:")) ? 6 : 4));
        if (!IsPositiveInteger(id)) {
            return false;
        }
        Str::Copy(m_move.location, MoveRequest::VALUE_CHARS, id);
        m_move.hasLocation = true;
        return true;
    } else if (IdAfterMarker(token, TEXT("/dcim/racks/"), id, MoveRequest::VALUE_CHARS)) {
        // fall through with a rack id
    } else if (IdAfterMarker(token, TEXT("/dcim/sites/"), id, MoveRequest::VALUE_CHARS)) {
        Str::Copy(m_move.site, MoveRequest::VALUE_CHARS, id);
        m_move.hasSite = true;
        return true;
    } else if (IdAfterMarker(token, TEXT("/dcim/locations/"), id, MoveRequest::VALUE_CHARS)) {
        Str::Copy(m_move.location, MoveRequest::VALUE_CHARS, id);
        m_move.hasLocation = true;
        return true;
    } else if (IsPositiveInteger(token)) {
        // A bare number typed at this prompt is a rack id; the hint says so.
        Str::Copy(id, MoveRequest::VALUE_CHARS, token);
    } else {
        return false;
    }

    if (!IsPositiveInteger(id)) {
        return false;
    }

    Str::Copy(m_move.rack, MoveRequest::VALUE_CHARS, id);
    m_move.hasRack = true;
    return true;
}

void Controller::CommitMove()
{
    if (m_targetId[0] == 0 || m_busy) {
        CancelPendingMove();
        ReturnToDevice(TEXT("Busy - try the move again"));
        return;
    }

    // Confirm before writing. A mistyped rack id is indistinguishable from a
    // correct one once it has been sent, and moving the wrong machine into the
    // wrong slot is precisely the failure this workflow exists to prevent.
    TCHAR prompt[MESSAGE_CHARS];
    prompt[0] = 0;
    Str::Append(prompt, MESSAGE_CHARS, TEXT("Move "));
    Str::Append(prompt, MESSAGE_CHARS, m_targetTitle);
    Str::Append(prompt, MESSAGE_CHARS, TEXT("\n"));

    // Only the destination the operator actually chose is named. A rack move
    // also carries the device's existing site and location, and repeating those
    // ids back as though they were part of the decision would bury the one line
    // that has to be checked.
    if (m_move.hasRack && m_move.rack[0] != 0) {
        Str::Append(prompt, MESSAGE_CHARS, TEXT("Rack "));
        Str::Append(prompt, MESSAGE_CHARS, m_move.rack);
        Str::Append(prompt, MESSAGE_CHARS, TEXT("  U"));
        Str::Append(prompt, MESSAGE_CHARS, m_move.position);
        Str::Append(prompt, MESSAGE_CHARS, TEXT("  "));
        Str::Append(prompt, MESSAGE_CHARS, m_move.face);
    } else if (m_move.hasSite && m_move.site[0] != 0) {
        Str::Append(prompt, MESSAGE_CHARS, TEXT("Site "));
        Str::Append(prompt, MESSAGE_CHARS, m_move.site);
        Str::Append(prompt, MESSAGE_CHARS, TEXT(", out of its rack"));
    } else if (m_move.hasLocation && m_move.location[0] != 0) {
        Str::Append(prompt, MESSAGE_CHARS, TEXT("Location "));
        Str::Append(prompt, MESSAGE_CHARS, m_move.location);
        Str::Append(prompt, MESSAGE_CHARS, TEXT(", out of its rack"));
    } else {
        Str::Append(prompt, MESSAGE_CHARS, TEXT("Out of its rack"));
    }

    if (!Views::ViewHelpers::ShowConfirm(m_mainWindow, TEXT("Confirm Move"), prompt)) {
        CancelPendingMove();
        ReturnToDevice(NULL);
        return;
    }

    BusyScope busy(&m_busy);

    NbClient* netbox = ActiveNetbox();
    bool sent = false;
    if (netbox && EnsureAuthenticated(false)) {
        sent = netbox->MoveDevice(m_targetId,
                                  Optional(m_move.hasSite, m_move.site),
                                  Optional(m_move.hasLocation, m_move.location),
                                  Optional(m_move.hasRack, m_move.rack),
                                  Optional(m_move.hasPosition, m_move.position),
                                  Optional(m_move.hasFace, m_move.face));
        if (!sent && RetryWithFreshToken()) {
            sent = netbox->MoveDevice(m_targetId,
                                      Optional(m_move.hasSite, m_move.site),
                                      Optional(m_move.hasLocation, m_move.location),
                                      Optional(m_move.hasRack, m_move.rack),
                                      Optional(m_move.hasPosition, m_move.position),
                                      Optional(m_move.hasFace, m_move.face));
        }
    }

    if (sent) {
        m_journal->LogTransaction(TEXT("DEVICE_MOVE"), m_targetId, TEXT("Moved from the handheld"));
        CancelPendingMove();

        // The rows below the header still describe where the device was. Saying
        // so is cheaper and more honest than a second blocking round trip that
        // the operator did not ask for.
        ReturnToDevice(TEXT("Moved - scan again to refresh"));
        return;
    }

    // Queue the whole positional set as one record. NetBox validates site,
    // location, rack, position and face against each other, so a move split
    // across two entries is rejected at replay time with an error that reaches
    // nobody who can act on it.
    Str::Buffer json;
    json.AppendChar((TCHAR)'{');
    json.AppendJsonPair(TEXT("id"), m_targetId);
    if (m_move.hasSite) {
        json.AppendChar((TCHAR)',');
        json.AppendJsonPair(TEXT("site"), m_move.site);
    }
    if (m_move.hasLocation) {
        json.AppendChar((TCHAR)',');
        json.AppendJsonPair(TEXT("location"), m_move.location);
    }
    if (m_move.hasRack) {
        json.AppendChar((TCHAR)',');
        json.AppendJsonPair(TEXT("rack"), m_move.rack);
    }
    if (m_move.hasPosition) {
        json.AppendChar((TCHAR)',');
        json.AppendJsonPair(TEXT("position"), m_move.position);
    }
    if (m_move.hasFace) {
        json.AppendChar((TCHAR)',');
        json.AppendJsonPair(TEXT("face"), m_move.face);
    }
    json.AppendChar((TCHAR)'}');

    Str::Buffer payload;
    payload.Append(TEXT("MOVE:"));
    payload.Append(json.Get());

    bool queued = false;
    if (!json.Failed() && !payload.Failed() && m_syncEngine) {
        queued = m_syncEngine->QueueTransaction(TEXT("DEVICE_MOVE"), payload.Get());
    }

    CancelPendingMove();

    if (queued) {
        m_journal->LogInfo(TEXT("Move queued for sync"));
        ReturnToDevice(TEXT("Offline - move queued"));
    } else {
        m_journal->LogError(TEXT("MOVE_QUEUE_FAILED"),
                            TEXT("Move could not be sent or queued"));
        ReturnToDevice(TEXT("Move was not saved"));
    }
}

// ---------------------------------------------------------------------------
// Picker plumbing
// ---------------------------------------------------------------------------

void Controller::ShowPicker(PickerPurpose purpose)
{
    m_pickerPurpose = purpose;
    ShowOnly(VIEW_PICKER);
}

void Controller::OnPickerChoice(const TCHAR* key, bool fromList)
{
    if (!key || key[0] == 0 || !m_pickerView) {
        return;
    }

    switch (m_pickerPurpose) {
    case PICK_BACKEND:
        m_pickerPurpose = PICK_NONE;
        SwitchBackend(key);
        return;

    case PICK_MATCH: {
        if (!fromList) {
            m_pickerView->SetMessage(TEXT("Press one of the row numbers"));
            return;
        }
        InventoryBackend* backend = GetActiveBackend();
        int index = 0;
        Models::AssetSummary chosen;
        if (backend && Str::ParseInt(key, &index) && backend->GetMatch(index, &chosen)) {
            m_pickerPurpose = PICK_NONE;
            ShowDeviceView(&chosen, index);
        } else {
            m_pickerView->SetMessage(TEXT("That match is no longer available"));
        }
        return;
    }

    case PICK_STATUS:
        m_pickerPurpose = PICK_NONE;
        ApplyStatus(key);
        return;

    case PICK_MOVE_DEST: {
        if (lstrcmp(key, kUnrackKey) == 0) {
            // Present and empty means "clear it": the device keeps its site and
            // location and stops being racked. All three go together because
            // NetBox will not accept a position without a rack.
            m_move.hasRack = true;
            m_move.hasPosition = true;
            m_move.hasFace = true;
            m_move.rack[0] = 0;
            m_move.position[0] = 0;
            m_move.face[0] = 0;
            m_pickerPurpose = PICK_NONE;
            CommitMove();
            return;
        }

        if (!ApplyDestinationToken(key)) {
            m_pickerView->SetMessage(TEXT("Not a rack, site or location label"));
            return;
        }

        if (m_move.hasRack) {
            // A rack only means anything alongside the site and location it
            // belongs to, and NetBox checks that relationship, so the PATCH
            // carries the device's current pair. They are deliberately not
            // cleared: if the new rack turns out to live somewhere else the
            // server rejects the move, which is the right outcome - quietly
            // blanking the location to make the request succeed would lose data
            // nobody asked to lose.
            if (m_targetSiteId[0] == 0) {
                m_pickerView->SetMessage(TEXT("Site unknown - scan the device again while online"));
                return;
            }
            Str::Copy(m_move.site, MoveRequest::VALUE_CHARS, m_targetSiteId);
            m_move.hasSite = true;
            Str::Copy(m_move.location, MoveRequest::VALUE_CHARS, m_targetLocationId);
            m_move.hasLocation = true;

            // Remember what was actually resolved, so the next move offers it
            // as a row instead of asking for the same label again.
            TCHAR historyKey[MoveRequest::VALUE_CHARS + 8];
            TCHAR historyLabel[Views::PickerView::LABEL_MAX];
            historyKey[0] = 0;
            historyLabel[0] = 0;
            Str::Append(historyKey, MoveRequest::VALUE_CHARS + 8, kRackKeyPrefix);
            Str::Append(historyKey, MoveRequest::VALUE_CHARS + 8, m_move.rack);
            Str::Append(historyLabel, Views::PickerView::LABEL_MAX, TEXT("Rack "));
            Str::Append(historyLabel, Views::PickerView::LABEL_MAX, m_move.rack);
            m_pickerView->Remember(historyKey, historyLabel, TEXT(""));

            AskMovePosition();
            return;
        }

        // A site or a location, not a rack. A device cannot stay racked in a
        // rack that belongs somewhere else, so the rack is cleared in the same
        // PATCH rather than left for NetBox to reject.
        m_move.hasRack = true;
        m_move.hasPosition = true;
        m_move.hasFace = true;
        m_move.rack[0] = 0;
        m_move.position[0] = 0;
        m_move.face[0] = 0;
        m_pickerPurpose = PICK_NONE;
        CommitMove();
        return;
    }

    case PICK_MOVE_POSITION:
        if (!IsDecimalNumber(key)) {
            m_pickerView->SetMessage(TEXT("Position must be a number, e.g. 12 or 42.5"));
            return;
        }
        Str::Copy(m_move.position, MoveRequest::VALUE_CHARS, key);
        m_move.hasPosition = true;
        m_pickerView->Remember(key, key, TEXT(""));
        AskMoveFace();
        return;

    case PICK_MOVE_FACE:
        if (lstrcmp(key, TEXT("front")) != 0 && lstrcmp(key, TEXT("rear")) != 0) {
            m_pickerView->SetMessage(TEXT("Press 1 for front or 2 for rear"));
            return;
        }
        Str::Copy(m_move.face, MoveRequest::VALUE_CHARS, key);
        m_move.hasFace = true;
        m_pickerPurpose = PICK_NONE;
        CommitMove();
        return;

    case PICK_NONE:
    default:
        return;
    }
}

void Controller::OnPickerCancel()
{
    const PickerPurpose purpose = m_pickerPurpose;
    m_pickerPurpose = PICK_NONE;

    // Abandon the whole move rather than sending what has been collected so
    // far: NetBox rejects an inconsistent positional set, and a half-applied
    // move is worse than none at all.
    if (purpose == PICK_MOVE_DEST || purpose == PICK_MOVE_POSITION ||
        purpose == PICK_MOVE_FACE) {
        CancelPendingMove();
    }

    if (purpose == PICK_BACKEND) {
        ShowScanView();
        return;
    }

    ReturnToDevice(NULL);
}

// ---------------------------------------------------------------------------
// Backend selection
// ---------------------------------------------------------------------------

void Controller::ShowBackendPicker()
{
    if (!m_syncEngine || !m_pickerView) {
        return;
    }

    if (m_syncEngine->GetBackendCount() < 2) {
        MessageBox(m_mainWindow,
                   TEXT("Only one inventory system is configured.\n")
                   TEXT("Add netboxBaseUrl to hb_conf.json to switch."),
                   TEXT("Switch System"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const InventoryBackend* active = m_syncEngine->GetActiveBackend();

    // Deliberately no history here: the order stays fixed so the digit that
    // selects a system is the same one every time.
    m_pickerView->BeginChoices(TEXT("Send new work to"), NULL);

    InventoryBackend* candidates[2];
    int candidateCount = 0;
    if (m_hbClient) {
        candidates[candidateCount++] = m_hbClient;
    }
    if (m_nbClient) {
        candidates[candidateCount++] = m_nbClient;
    }

    for (int i = 0; i < candidateCount; i++) {
        InventoryBackend* backend = candidates[i];
        const TCHAR* instanceId = backend->GetInstanceId();

        // Only registered backends can be made active, and registration can be
        // refused (an unusable instance id), so ask the registry rather than
        // assuming.
        if (!m_syncEngine->FindBackend(instanceId)) {
            continue;
        }

        m_pickerView->AddChoice(instanceId, backend->GetDisplayName(),
                                (backend == active) ? TEXT("in use") : instanceId, NULL);
    }

    m_pickerView->SetInputHint(TEXT("Press the row number"));
    m_pickerView->EndChoices();
    ShowPicker(PICK_BACKEND);
}

void Controller::SwitchBackend(const TCHAR* instanceId)
{
    if (!m_syncEngine || !m_config || !instanceId) {
        return;
    }

    if (!m_syncEngine->SetActiveBackend(instanceId)) {
        MessageBox(m_mainWindow, TEXT("That inventory system is not configured."),
                   TEXT("Switch System"), MB_OK | MB_ICONERROR);
        ShowScanView();
        return;
    }

    // Selecting who takes NEW work is all this does. Every configured backend
    // stays registered, so anything already queued for the system just left
    // still replays to it - draining or discarding that queue here would throw
    // away work the operator has done and cannot redo.
    m_config->SetActiveBackendId(instanceId);

    const TCHAR* configPath = m_config->GetConfigPath();
    if (!configPath || !m_config->Save(configPath)) {
        // Not fatal: the switch holds for this session, it just will not
        // survive the next battery swap.
        m_journal->LogError(TEXT("BACKEND_SAVE_FAILED"),
                            TEXT("Active backend could not be written to hb_conf.json"));
    }

    // Anything still on screen or half-entered belongs to the system just left.
    CancelPendingMove();
    ClearPendingLocationScan();
    m_locationMode = false;
    m_targetId[0] = 0;
    m_targetTitle[0] = 0;
    m_targetSiteId[0] = 0;
    m_targetLocationId[0] = 0;
    if (m_deviceView) {
        m_deviceView->Clear();
    }

    m_journal->LogInfo(TEXT("Active inventory system changed"));

    ShowScanView();
    if (m_scanView) {
        m_scanView->SetStatus(TEXT("Ready to scan"));
    }
    UpdateUI();
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
    ConfigureBackends();

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
    m_deviceView = new Views::DeviceView();
    m_pickerView = new Views::PickerView();

    if (!m_scanView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }
    if (!m_queueView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }
    if (!m_itemView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }
    if (!m_deviceView->Create(m_mainWindow, m_hInstance)) {
        return false;
    }
    if (!m_pickerView->Create(m_mainWindow, m_hInstance)) {
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

    // The read-only detail screen and the chooser both hand every decision back
    // here; neither knows anything about a backend.
    m_deviceView->SetActionCallback(&Controller::DeviceActionThunk, this);
    m_pickerView->SetChooseCallback(&Controller::PickerChooseThunk, this);
    m_pickerView->SetCancelCallback(&Controller::PickerCancelThunk, this);

    // Only the scan view is visible initially.
    ShowOnly(VIEW_SCAN);
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
    if (m_deviceView && m_deviceView->GetHandle()) {
        MoveWindow(m_deviceView->GetHandle(), 0, 0, width, height, TRUE);
    }
    if (m_pickerView && m_pickerView->GetHandle()) {
        MoveWindow(m_pickerView->GetHandle(), 0, 0, width, height, TRUE);
    }
}

void Controller::ShowOnly(ActiveView view)
{
    // One place decides what is visible. Hiding the other screens by hand at
    // every navigation site is quadratic in the number of views, and a single
    // missed Show(false) leaves two of them stacked on a 240 px screen with no
    // way to tell which one the buttons belong to.
    if (m_scanView)   m_scanView->Show(view == VIEW_SCAN);
    if (m_queueView)  m_queueView->Show(view == VIEW_QUEUE);
    if (m_itemView)   m_itemView->Show(view == VIEW_ITEM);
    if (m_deviceView) m_deviceView->Show(view == VIEW_DEVICE);
    if (m_pickerView) m_pickerView->Show(view == VIEW_PICKER);

    m_activeView = view;
}

void Controller::ShowScanView()
{
    // Leaving for the scan screen ends whatever the picker was asking.
    m_pickerPurpose = PICK_NONE;
    ShowOnly(VIEW_SCAN);
}

void Controller::ShowQueueView()
{
    if (m_queueView) {
        m_queueView->RefreshQueue();   // pull the latest pending transactions
        m_queueView->UpdateSyncStatus(m_syncEngine ? m_syncEngine->GetSyncStatus()
                                                   : SyncEngine::SYNC_IDLE);
    }
    ShowOnly(VIEW_QUEUE);
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
    ShowOnly(VIEW_ITEM);
}

void Controller::ShowDeviceView(const Models::AssetSummary* asset, int matchIndex)
{
    if (!m_deviceView || !asset) {
        return;
    }

    m_deviceView->DisplayAsset(asset);

    InventoryBackend* backend = GetActiveBackend();

    // The summary carries site, location and rack as names, because names are
    // what an operator reads. A move has to send ids, so they are copied out of
    // the parsed record now: the backend only keeps it until the next lookup,
    // and a scan while the move is half entered would otherwise leave the
    // positional set describing a different device.
    m_targetSiteId[0] = 0;
    m_targetLocationId[0] = 0;

    NbClient* netbox = ActiveNetbox();
    if (netbox) {
        const Models::Device* device = netbox->GetMatchedDevice(matchIndex);
        if (device) {
            Str::Copy(m_targetSiteId, MoveRequest::VALUE_CHARS, device->GetSiteId());
            Str::Copy(m_targetLocationId, MoveRequest::VALUE_CHARS, device->GetLocationId());
        }
    }

    // Capability, not identity. The status action is offered exactly when the
    // backend says it has statuses. The move action is offered when this class
    // holds the typed client that can perform a structured move - the neutral
    // interface carries no move operation for a backend to advertise, so there
    // is nothing better to ask.
    m_deviceView->SetActionsAvailable(ActiveNetbox() != NULL,
                                      backend != NULL && backend->SupportsStatus());
    m_deviceView->SetNotice(NULL);

    ShowOnly(VIEW_DEVICE);
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

// static
void Controller::DeviceActionThunk(int action, void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->OnDeviceAction(action);
    }
}

// static
void Controller::PickerChooseThunk(const TCHAR* key, bool fromList, void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->OnPickerChoice(key, fromList);
    }
}

// static
void Controller::PickerCancelThunk(void* userData)
{
    Controller* self = (Controller*)userData;
    if (self) {
        self->OnPickerCancel();
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

    // The active system leads the title in every state and on every screen.
    // With two backends configured, acting on the wrong one is the failure that
    // matters, and the title bar is the only strip present on all five views -
    // so it names the system rather than the product.
    InventoryBackend* backend = GetActiveBackend();
    Str::Copy(title, kTitleCap, backend ? backend->GetDisplayName() : TEXT("HBX Client"));

    switch (m_state) {
    case STATE_INIT:
        Str::Append(title, kTitleCap, TEXT(" - Initializing..."));
        break;

    case STATE_IDLE:
        // The connection state matters more than anything else on this screen:
        // an unauthenticated device silently queues everything it scans.
        Str::Append(title, kTitleCap,
                    (backend && backend->IsAuthenticated()) ? TEXT(" - Ready")
                                                            : TEXT(" - Offline"));
        Str::Append(title, kTitleCap, TEXT(" [Q:"));
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
    // UpdateUI replaces this with the active system's name as soon as the state
    // is known; it only has to be neutral until then.
    m_mainWindow = CreateWindow(
        kMainWindowClass,
        TEXT("HBX Client"),
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
            case IDM_ACTION_SWITCHBACKEND:
                pController->ShowBackendPicker();
                return 0;
            case IDM_HELP_ABOUT:
                MessageBox(hwnd, TEXT("HBX Client\nVersion 1.0.0"),
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
