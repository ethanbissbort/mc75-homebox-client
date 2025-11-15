#include "../include/Controller.hpp"
#include <commctrl.h>

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
{
}

Controller::~Controller()
{
}

bool Controller::Initialize(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES;
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

    // TODO: Lookup item and display
    // TODO: Queue transaction if offline

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
    return CreateMainWindow();
}

void Controller::UpdateUI()
{
    // TODO: Update UI based on current state
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

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

} // namespace HBX
