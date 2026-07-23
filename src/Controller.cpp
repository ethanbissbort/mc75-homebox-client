#include "../include/Controller.hpp"
#include <commctrl.h>
#include <aygshell.h>
#include "../resources/resource.h"

namespace HBX {

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
{
}

Controller::~Controller()
{
}

bool Controller::Initialize(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

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

    // Load configuration
    if (!m_config->Load(TEXT("\\Program Files\\HBXClient\\hb_conf.json")))
    {
        // Use defaults if load fails
    }

    // Initialize journal
    if (!m_journal->Initialize(TEXT("\\Program Files\\HBXClient\\hbx.journal")))
    {
        return false;
    }

    // Configure API client
    m_hbClient->SetBaseUrl(m_config->GetApiBaseUrl());

    // Initialize scanner
    if (!m_scanner->Initialize())
    {
        m_journal->LogError(TEXT("SCANNER_INIT"), TEXT("Failed to initialize scanner hardware"));
        // Continue anyway - scanner might not be available
    }

    // Create main window and UI
    if (!InitializeUI())
    {
        return false;
    }

    m_journal->LogInfo(TEXT("Application initialized successfully"));
    SetState(STATE_IDLE);

    return true;
}

int Controller::Run()
{
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
    // Tear down the UI views first (they hold non-owning references to the
    // scanner / sync engine, so they must go before those are deleted).
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

    // Shutdown scanner
    if (m_scanner) {
        m_scanner->Shutdown();
        delete m_scanner;
        m_scanner = NULL;
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

    // Destroy main window
    if (m_mainWindow) {
        DestroyWindow(m_mainWindow);
        m_mainWindow = NULL;
    }
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

void Controller::OnScanReceived(const TCHAR* barcode)
{
    if (!barcode || lstrlen(barcode) == 0) {
        return;
    }

    m_journal->LogTransaction(TEXT("SCAN"), barcode, TEXT("Barcode scanned"));

    SetState(STATE_SCANNING);

    // Try to lookup item from API
    Models::Item item;
    bool success = m_hbClient->GetItem(barcode, &item);

    if (success && item.IsValid()) {
        // Item found - display it
        TCHAR message[512];
        wsprintf(message, TEXT("Item Found:\n%s\nBarcode: %s\nLocation: %s\nQuantity: %d"),
                 item.GetName() ? item.GetName() : TEXT("Unknown"),
                 item.GetBarcode() ? item.GetBarcode() : TEXT(""),
                 item.GetLocationId() ? item.GetLocationId() : TEXT("None"),
                 item.GetQuantity());

        MessageBox(m_mainWindow, message, TEXT("Item Details"), MB_OK | MB_ICONINFORMATION);

        m_journal->LogInfo(TEXT("Item lookup successful"));
    } else {
        // Item not found or offline - queue the transaction
        if (!m_syncEngine->IsOnline()) {
            // Queue transaction for later sync
            TCHAR transactionData[512];
            wsprintf(transactionData, TEXT("SCAN:%s"), barcode);

            if (m_syncEngine->QueueTransaction(TEXT("ITEM_SCAN"), transactionData)) {
                MessageBox(m_mainWindow,
                          TEXT("Device is offline. Scan queued for synchronization."),
                          TEXT("Offline Mode"),
                          MB_OK | MB_ICONWARNING);

                m_journal->LogInfo(TEXT("Scan queued for offline sync"));
            } else {
                MessageBox(m_mainWindow,
                          TEXT("Failed to queue transaction."),
                          TEXT("Error"),
                          MB_OK | MB_ICONERROR);

                m_journal->LogError(TEXT("QUEUE_FAILED"), TEXT("Failed to queue offline transaction"));
            }
        } else {
            // Online but item not found
            MessageBox(m_mainWindow,
                      TEXT("Item not found in database."),
                      TEXT("Not Found"),
                      MB_OK | MB_ICONWARNING);

            m_journal->LogInfo(TEXT("Item not found"));
        }
    }

    SetState(STATE_IDLE);
}

void Controller::OnSyncRequested()
{
    SetState(STATE_SYNCING);

    if (m_syncEngine->Sync()) {
        m_journal->LogInfo(TEXT("Sync completed successfully"));
    } else {
        m_journal->LogError(TEXT("SYNC_FAILED"), m_syncEngine->GetLastSyncError());
    }

    SetState(STATE_IDLE);
}

void Controller::OnConfigChanged()
{
    // Reload configuration
    m_config->Load(TEXT("\\Program Files\\HBXClient\\hb_conf.json"));
    m_hbClient->SetBaseUrl(m_config->GetApiBaseUrl());
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

    // Item editor saves flow back through the controller to the API client.
    m_itemView->SetSaveCallback(&Controller::ItemSaveThunk, this);

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
    return false;
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
        m_queueView->Show(true);
    }
    if (m_scanView) m_scanView->Show(false);
    if (m_itemView) m_itemView->Show(false);
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

void Controller::OnItemSave(const Models::Item* item)
{
    if (!item || !m_hbClient) {
        return;
    }

    // Existing item (has an id) -> update; otherwise create.
    bool hasId = (item->GetId() != NULL && lstrlen(item->GetId()) > 0);
    bool ok = hasId ? m_hbClient->UpdateItem(item) : m_hbClient->CreateItem(item);

    if (ok) {
        m_journal->LogInfo(TEXT("Item saved to server"));
        MessageBox(m_mainWindow, TEXT("Item saved."), TEXT("Saved"), MB_OK | MB_ICONINFORMATION);
    } else if (m_syncEngine && !m_syncEngine->IsOnline()) {
        // Offline: queue the update as an ITEM_UPDATE transaction for later sync.
        TCHAR* json = item->ToJson();
        if (json) {
            TCHAR data[2100];
            wsprintf(data, TEXT("UPDATE:%s"), json);
            m_syncEngine->QueueTransaction(TEXT("ITEM_UPDATE"), data);
            delete[] json;
        }
        MessageBox(m_mainWindow, TEXT("Offline - item queued for sync."),
                   TEXT("Offline Mode"), MB_OK | MB_ICONWARNING);
    } else {
        MessageBox(m_mainWindow, TEXT("Failed to save item."),
                   TEXT("Error"), MB_OK | MB_ICONERROR);
    }
}

void Controller::UpdateUI()
{
    if (!m_mainWindow) {
        return;
    }

    // Update window title based on state
    TCHAR title[128];

    switch (m_state) {
    case STATE_INIT:
        lstrcpy(title, TEXT("HomeBox Client - Initializing..."));
        break;

    case STATE_IDLE:
        wsprintf(title, TEXT("HomeBox Client - Ready [Queue: %d]"),
                m_syncEngine ? m_syncEngine->GetQueuedTransactionCount() : 0);
        break;

    case STATE_SCANNING:
        lstrcpy(title, TEXT("HomeBox Client - Scanning..."));
        break;

    case STATE_SYNCING:
        lstrcpy(title, TEXT("HomeBox Client - Syncing..."));
        break;

    case STATE_ERROR:
        lstrcpy(title, TEXT("HomeBox Client - Error"));
        break;

    default:
        lstrcpy(title, TEXT("HomeBox Client"));
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
    wc.lpszClassName = TEXT("HBXClientWndClass");

    if (!RegisterClass(&wc)) {
        return false;
    }

    // Create main window
    m_mainWindow = CreateWindow(
        TEXT("HBXClientWndClass"),
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
            case IDM_ACTION_SYNC:
                pController->OnSyncRequested();
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
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

} // namespace HBX
