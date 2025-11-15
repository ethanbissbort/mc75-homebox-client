#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <windows.h>
#include "Config.hpp"
#include "HbClient.hpp"
#include "SyncEngine.hpp"
#include "Journal.hpp"
#include "ScannerHAL.hpp"

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
    SyncEngine* GetSyncEngine();
    Journal* GetJournal();
    ScannerHAL* GetScanner();

    // Event handlers
    void OnScanReceived(const TCHAR* barcode);
    void OnSyncRequested();
    void OnConfigChanged();

private:
    HINSTANCE m_hInstance;
    HWND m_mainWindow;
    AppState m_state;

    // Core components
    Config* m_config;
    HbClient* m_hbClient;
    SyncEngine* m_syncEngine;
    Journal* m_journal;
    ScannerHAL* m_scanner;

    // UI management
    bool InitializeUI();
    void UpdateUI();
    bool CreateMainWindow();

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

} // namespace HBX

#endif // CONTROLLER_HPP
