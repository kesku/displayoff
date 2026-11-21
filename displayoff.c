#include "power.h"
#include <shellapi.h>

enum {
    ICON_RESOURCE = 1,
    TRAY_MESSAGE = WM_APP + 1,
    BLANK_MESSAGE,
    START_TIMER = 1,
    RESTORE_TIMER,
    EXIT_COMMAND = 1,
};

static const wchar_t title[] = L"Display Off";
static const wchar_t window_class[] = L"DisplayOff.Tray";
static const wchar_t instance_name[] = L"Local\\DisplayOff_SingleInstance";
static HWND window;
static UINT taskbar_created;
static NOTIFYICONDATAW tray;
static HHOOK keyboard;
static HANDLE blank_mutex;
static BOOL owns_blank_mutex;
static BOOL blank_pending;
static BOOL hotkey_down;
static BOOL tray_ready;
static BOOL tray_v4;
static DWORD last_click;
static BOOL saw_click;

static BOOL add_tray(void)
{
    if (!Shell_NotifyIconW(NIM_ADD, &tray))
        return FALSE;
    tray.uVersion = NOTIFYICON_VERSION_4;
    tray_v4 = Shell_NotifyIconW(NIM_SETVERSION, &tray);
    return TRUE;
}

static void notice(const wchar_t *message)
{
    if (!tray_ready) {
        MessageBoxW(NULL, message, title, MB_OK | MB_ICONERROR);
        return;
    }
    NOTIFYICONDATAW info = tray;
    info.uFlags = NIF_INFO;
    info.dwInfoFlags = NIIF_WARNING;
    lstrcpynW(info.szInfoTitle, title, ARRAYSIZE(info.szInfoTitle));
    lstrcpynW(info.szInfo, message, ARRAYSIZE(info.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &info);
}

static void start_blank(HWND hwnd)
{
    if (blank_pending || GetSystemMetrics(SM_REMOTESESSION))
        return;
    blank_pending = SetTimer(hwnd, START_TIMER, 500, NULL) != 0;
    if (!blank_pending)
        notice(L"Couldn’t turn off displays.");
}

static void finish_blank(HWND hwnd)
{
    KillTimer(hwnd, START_TIMER);
    KillTimer(hwnd, RESTORE_TIMER);
    if (owns_blank_mutex) {
        DWORD status = displayoff_restore();
        ReleaseMutex(blank_mutex);
        owns_blank_mutex = FALSE;
        if (status != ERROR_SUCCESS)
            notice(L"Couldn’t restore the display timeout. Restart Display Off to retry.");
    }
    blank_pending = FALSE;
}

static void begin_blank(HWND hwnd)
{
    KillTimer(hwnd, START_TIMER);
    DWORD wait = WaitForSingleObject(blank_mutex, 0);
    owns_blank_mutex = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    if (!owns_blank_mutex) {
        blank_pending = FALSE;
        return;
    }

    // Keep the same settle and idle window as the upstream Windows path.
    if (displayoff_begin() != ERROR_SUCCESS ||
        !SetTimer(hwnd, RESTORE_TIMER, 5000, NULL)) {
        finish_blank(hwnd);
        notice(L"Couldn’t turn off displays.");
    }
}

static LRESULT CALLBACK hotkey(int code, WPARAM event, LPARAM data)
{
    if (code == HC_ACTION && ((const KBDLLHOOKSTRUCT *)data)->vkCode == VK_F12) {
        if (event == WM_KEYUP || event == WM_SYSKEYUP) {
            if (hotkey_down) {
                hotkey_down = FALSE;
                return 1;
            }
        } else if (event == WM_KEYDOWN || event == WM_SYSKEYDOWN) {
            BOOL matches = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                (GetAsyncKeyState(VK_MENU) & 0x8000) &&
                !(GetAsyncKeyState(VK_RMENU) & 0x8000) &&
                !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
                !(GetAsyncKeyState(VK_RWIN) & 0x8000);
            if (matches && !hotkey_down) {
                hotkey_down = PostMessageW(window, BLANK_MESSAGE, 0, 0);
            }
            if (hotkey_down)
                return 1;
        }
    }
    return CallNextHookEx(NULL, code, event, data);
}

static void tray_menu(HWND hwnd)
{
    POINT point;
    if (!GetCursorPos(&point))
        return;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, EXIT_COMMAND, L"Exit");
    SetForegroundWindow(hwnd);
    UINT command = TrackPopupMenu(menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (command == EXIT_COMMAND)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (taskbar_created && message == taskbar_created) {
        tray_ready = add_tray();
        return 0;
    }

    switch (message) {
    case TRAY_MESSAGE:
        switch (LOWORD(lparam)) {
        case WM_LBUTTONDBLCLK:
        case NIN_KEYSELECT:
            saw_click = FALSE;
            start_blank(hwnd);
            break;
        case WM_LBUTTONUP: {
            if (blank_pending)
                break;
            DWORD now = GetMessageTime();
            if (saw_click && now - last_click <= GetDoubleClickTime()) {
                saw_click = FALSE;
                start_blank(hwnd);
            } else {
                last_click = now;
                saw_click = TRUE;
            }
            break;
        }
        case WM_RBUTTONUP:
            if (tray_v4)
                break;
            /* fall through */
        case WM_CONTEXTMENU:
            saw_click = FALSE;
            tray_menu(hwnd);
            break;
        }
        return 0;
    case BLANK_MESSAGE:
        start_blank(hwnd);
        return 0;
    case WM_TIMER:
        if (wparam == START_TIMER && blank_pending && !owns_blank_mutex)
            begin_blank(hwnd);
        if (wparam == RESTORE_TIMER && owns_blank_mutex)
            finish_blank(hwnd);
        return 0;
    case WM_QUERYENDSESSION:
        finish_blank(hwnd);
        return TRUE;
    case WM_DESTROY:
        finish_blank(hwnd);
        if (keyboard)
            UnhookWindowsHookEx(keyboard);
        if (tray_ready)
            Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show)
{
    (void)previous;
    (void)show;
    BOOL turn_off = lstrcmpW(args, L"--off") == 0;
    HANDLE single_instance = CreateMutexW(NULL, FALSE, instance_name);
    DWORD mutex_status = GetLastError();
    if (!single_instance)
        return 1;
    if (mutex_status == ERROR_ALREADY_EXISTS) {
        // The first process may still be creating its tray window.
        for (int attempt = 0; turn_off && attempt < 40; ++attempt) {
            HWND running = FindWindowW(window_class, title);
            if (running && PostMessageW(running, BLANK_MESSAGE, 0, 0)) {
                CloseHandle(single_instance);
                return 0;
            }
            Sleep(25);
        }
        CloseHandle(single_instance);
        return turn_off ? 1 : 0;
    }

    blank_mutex = CreateMutexW(NULL, FALSE, DISPLAYOFF_BLANK_MUTEX);
    if (!blank_mutex) {
        CloseHandle(single_instance);
        return 1;
    }
    DWORD wait = WaitForSingleObject(blank_mutex, 0);
    DWORD recovered = ERROR_SUCCESS;
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        recovered = displayoff_restore();
        ReleaseMutex(blank_mutex);
    }

    WNDCLASSW cls = { .lpfnWndProc = handle_message, .hInstance = instance,
        .lpszClassName = window_class };
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (RegisterClassW(&cls))
        window = CreateWindowExW(0, window_class, title, 0, 0, 0, 0, 0,
            NULL, NULL, instance, NULL);

    int result = 1;
    if (window) {
        tray.cbSize = sizeof tray;
        tray.hWnd = window;
        tray.uID = ICON_RESOURCE;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        tray.uCallbackMessage = TRAY_MESSAGE;
        tray.hIcon = LoadImageW(instance, MAKEINTRESOURCEW(ICON_RESOURCE),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_SHARED);
        lstrcpyW(tray.szTip, title);
        tray_ready = add_tray();
        if (tray_ready) {
            keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, hotkey, instance, 0);
            if (!keyboard)
                notice(L"Couldn’t enable Ctrl+Alt+F12.");
            if (recovered != ERROR_SUCCESS)
                notice(L"Couldn’t restore the display timeout. Restart Display Off to retry.");
            if (turn_off)
                start_blank(window);

            MSG msg;
            BOOL status;
            while ((status = GetMessageW(&msg, NULL, 0, 0)) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            result = status < 0 ? 1 : 0;
        } else {
            notice(L"Couldn’t open Display Off in the system tray.");
        }
        if (IsWindow(window))
            DestroyWindow(window);
    }
    CloseHandle(blank_mutex);
    CloseHandle(single_instance);
    return result;
}
