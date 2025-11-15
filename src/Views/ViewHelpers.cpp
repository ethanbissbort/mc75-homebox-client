#include "../../include/Views/ViewHelpers.hpp"

namespace HBX {
namespace Views {

void ViewHelpers::ShowInfo(HWND parent, const TCHAR* title, const TCHAR* message)
{
    MessageBox(parent, message, title, MB_OK | MB_ICONINFORMATION);
}

void ViewHelpers::ShowError(HWND parent, const TCHAR* title, const TCHAR* message)
{
    MessageBox(parent, message, title, MB_OK | MB_ICONERROR);
}

bool ViewHelpers::ShowConfirm(HWND parent, const TCHAR* title, const TCHAR* message)
{
    return (IDYES == MessageBox(parent, message, title, MB_YESNO | MB_ICONQUESTION));
}

HWND ViewHelpers::CreateButton(HWND parent, const TCHAR* text, int x, int y, int width, int height, int id)
{
    return CreateWindow(
        TEXT("BUTTON"),
        text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, width, height,
        parent,
        (HMENU)id,
        NULL,
        NULL
    );
}

HWND ViewHelpers::CreateLabel(HWND parent, const TCHAR* text, int x, int y, int width, int height)
{
    return CreateWindow(
        TEXT("STATIC"),
        text,
        WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        parent,
        NULL,
        NULL,
        NULL
    );
}

HWND ViewHelpers::CreateEditBox(HWND parent, const TCHAR* text, int x, int y, int width, int height, int id, bool multiline)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT;
    if (multiline) {
        style |= ES_MULTILINE | ES_AUTOVSCROLL;
    }
    
    return CreateWindow(
        TEXT("EDIT"),
        text,
        style,
        x, y, width, height,
        parent,
        (HMENU)id,
        NULL,
        NULL
    );
}

HWND ViewHelpers::CreateListView(HWND parent, int x, int y, int width, int height, int id)
{
    return CreateWindow(
        WC_LISTVIEW,
        TEXT(""),
        WS_CHILD | WS_VISIBLE | LVS_REPORT,
        x, y, width, height,
        parent,
        (HMENU)id,
        NULL,
        NULL
    );
}

void ViewHelpers::CenterWindow(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    int x = (screenWidth - width) / 2;
    int y = (screenHeight - height) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void ViewHelpers::GetClientSize(HWND hwnd, int* width, int* height)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (width) *width = rc.right - rc.left;
    if (height) *height = rc.bottom - rc.top;
}

void ViewHelpers::SetControlPosition(HWND ctrl, int x, int y)
{
    SetWindowPos(ctrl, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void ViewHelpers::SetControlSize(HWND ctrl, int width, int height)
{
    SetWindowPos(ctrl, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
}

TCHAR* ViewHelpers::DuplicateString(const TCHAR* str)
{
    if (!str) return NULL;
    int len = lstrlen(str) + 1;
    TCHAR* dup = new TCHAR[len];
    lstrcpy(dup, str);
    return dup;
}

void ViewHelpers::FreeString(TCHAR* str)
{
    if (str) delete[] str;
}

bool ViewHelpers::StringsEqual(const TCHAR* str1, const TCHAR* str2)
{
    if (!str1 || !str2) return (str1 == str2);
    return (lstrcmp(str1, str2) == 0);
}

int ViewHelpers::StringLength(const TCHAR* str)
{
    return str ? lstrlen(str) : 0;
}

void ViewHelpers::StringCopy(TCHAR* dest, const TCHAR* src, int maxLen)
{
    if (dest && src) {
        lstrcpyn(dest, src, maxLen);
    }
}

int ViewHelpers::GetScreenWidth()
{
    return GetSystemMetrics(SM_CXSCREEN);
}

int ViewHelpers::GetScreenHeight()
{
    return GetSystemMetrics(SM_CYSCREEN);
}

bool ViewHelpers::IsPortrait()
{
    return GetScreenHeight() > GetScreenWidth();
}

HFONT ViewHelpers::CreateDefaultFont()
{
    return (HFONT)GetStockObject(SYSTEM_FONT);
}

HFONT ViewHelpers::CreateBoldFont()
{
    LOGFONT lf = {0};
    lf.lfHeight = -12;
    lf.lfWeight = FW_BOLD;
    lstrcpy(lf.lfFaceName, TEXT("Tahoma"));
    return CreateFontIndirect(&lf);
}

HFONT ViewHelpers::CreateLargeFont()
{
    LOGFONT lf = {0};
    lf.lfHeight = -16;
    lf.lfWeight = FW_NORMAL;
    lstrcpy(lf.lfFaceName, TEXT("Tahoma"));
    return CreateFontIndirect(&lf);
}

void ViewHelpers::DeleteFont(HFONT font)
{
    if (font && font != GetStockObject(SYSTEM_FONT)) {
        DeleteObject(font);
    }
}

} // namespace Views
} // namespace HBX
