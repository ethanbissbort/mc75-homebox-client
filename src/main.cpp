#include <windows.h>
#include "../include/Controller.hpp"

using namespace HBX;

/**
 * WinMain - Application entry point for Windows Mobile
 * @param hInstance Current instance handle
 * @param hPrevInstance Previous instance handle (always NULL in Win32)
 * @param lpCmdLine Command line arguments
 * @param nCmdShow Show window state
 * @return Exit code
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    // Initialize controller
    Controller* controller = new Controller();

    if (!controller->Initialize(hInstance))
    {
        MessageBox(NULL, TEXT("Failed to initialize application"), TEXT("Error"), MB_OK | MB_ICONERROR);
        delete controller;
        return 1;
    }

    // Run main message loop
    int exitCode = controller->Run();

    // Cleanup
    controller->Shutdown();
    delete controller;

    return exitCode;
}
