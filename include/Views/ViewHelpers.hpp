#ifndef VIEWS_VIEWHELPERS_HPP
#define VIEWS_VIEWHELPERS_HPP

#include <windows.h>

namespace HBX {
namespace Views {

/**
 * UI utility functions
 * Common helpers for view components
 */
class ViewHelpers {
public:
    // Message boxes
    static void ShowInfo(HWND parent, const TCHAR* title, const TCHAR* message);
    static void ShowError(HWND parent, const TCHAR* title, const TCHAR* message);
    static bool ShowConfirm(HWND parent, const TCHAR* title, const TCHAR* message);

    // Control creation
    static HWND CreateButton(HWND parent, const TCHAR* text, int x, int y, int width, int height, int id);
    static HWND CreateLabel(HWND parent, const TCHAR* text, int x, int y, int width, int height);
    static HWND CreateEditBox(HWND parent, const TCHAR* text, int x, int y, int width, int height, int id, bool multiline = false);
    static HWND CreateListView(HWND parent, int x, int y, int width, int height, int id);

    // Layout helpers
    static void CenterWindow(HWND hwnd);
    static void GetClientSize(HWND hwnd, int* width, int* height);
    static void SetControlPosition(HWND ctrl, int x, int y);
    static void SetControlSize(HWND ctrl, int width, int height);

    // String utilities
    static TCHAR* DuplicateString(const TCHAR* str);
    static void FreeString(TCHAR* str);
    static bool StringsEqual(const TCHAR* str1, const TCHAR* str2);
    static int StringLength(const TCHAR* str);
    static void StringCopy(TCHAR* dest, const TCHAR* src, int maxLen);

    // Device info
    static int GetScreenWidth();
    static int GetScreenHeight();
    static bool IsPortrait();

    // Font helpers
    static HFONT CreateDefaultFont();
    static HFONT CreateBoldFont();
    static HFONT CreateLargeFont();
    static void DeleteFont(HFONT font);
};

} // namespace Views
} // namespace HBX

#endif // VIEWS_VIEWHELPERS_HPP
