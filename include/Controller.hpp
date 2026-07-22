#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <windows.h>
#include "Config.hpp"
#include "HbClient.hpp"
#include "SyncEngine.hpp"
#include "Journal.hpp"
#include "ScannerHAL.hpp"
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
    SyncEngine* GetSyncEngine();
    Journal* GetJournal();
    ScannerHAL* GetScanner();

    // Event handlers
    void OnScanReceived(const TCHAR* barcode);
    void OnSyncRequested();
    void OnConfigChanged();
    void OnItemSave(const Models::Item* item);

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

    // UI views (children of the main window)
    Views::ScanView* m_scanView;
    Views::QueueView* m_queueView;
    Views::ItemView* m_itemView;
    HWND m_menuBar;

    // UI management
    bool InitializeUI();
    void UpdateUI();
    bool CreateMainWindow();
    bool CreateViews();
    bool CreateMenuBar();
    void ShowScanView();
    void ShowQueueView();

    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Trampolines that forward view callbacks into the controller.
    static void ScanCallbackThunk(const TCHAR* barcode, void* userData);
    static void SyncCallbackThunk(void* userData);
    static void ItemSaveThunk(const Models::Item* item, void* userData);
};

} // namespace HBX

#endif // CONTROLLER_HPP
