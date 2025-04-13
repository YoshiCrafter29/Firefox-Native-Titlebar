// ==WindhawkMod==
// @id              firefox-native-titlebar
// @name            Firefox Native Titlebar
// @description     Restores native titlebar buttons in Firefox
// @version         0.1
// @author          YoshiCrafter29
// @github          https://github.com/YoshiCrafter29
// @homepage        https://yoshicrafter29.bsky.social/
// @include         librewolf.exe
// @include         firefox.exe
// @compilerOptions -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Firefox Native Titlebar

Readds the native titlebar to Firefox versions (without xul patch)

### Check the [GitHub Repository](https://www.github.com/YoshiCrafter29/FirefoxNativeTitlebar) for installation instructions, as it requires an `userChrome.css` file
*/
// ==/WindhawkModReadme==

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#include <cstddef>
#include <cstdlib>

WNDPROC mozWndProc;
int wndProcAddress = 0;
const LPCWSTR mozWindowClass = L"MozillaWindowClass";

using RegisterClassW_t = decltype(&RegisterClassW);
decltype(&SetWindowLongPtrW) SetWindowLongPtrW_Original;
decltype(&GetWindowLongPtrW) GetWindowLongPtrW_Original;
decltype(&DwmExtendFrameIntoClientArea) DwmExtendFrameIntoClientArea_Original;

// Checks whenever the Firefox window is a window with a caption (windowed instead of fullscreen)
bool isMozWindowed(HWND hWnd) {
    return (GetWindowLongPtrW_Original(hWnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

// Checks if the cursor is in the same area as the titlebar controls area.
// This prevents normal browser activity from being disrupted
bool isCursorInControlsArea(HWND hWnd, POINT &p) {
    RECT windowRect;
    GetWindowRect(hWnd, &windowRect);

    POINT offset = {
        (GetSystemMetricsForDpi(SM_CXSIZE, GetDpiForWindow(hWnd)) * 3) + (GetSystemMetricsForDpi(SM_CXSIZEFRAME, GetDpiForWindow(hWnd)) * 2),
        GetSystemMetricsForDpi(SM_CYSIZE, GetDpiForWindow(hWnd)) + (GetSystemMetricsForDpi(SM_CYBORDER, GetDpiForWindow(hWnd)) * 2)};

    return ((p.x >= windowRect.right - offset.x && p.x < windowRect.right) && (p.y >= windowRect.top && p.y <= windowRect.top + offset.y));
}

// Checks if the class of the window sent in parameter corresponds to className
bool isOfWinClass(HWND hWnd, LPCWSTR className) {
    LPWSTR claName = (LPWSTR)calloc(50, sizeof(WCHAR));
    GetClassName(hWnd, claName, 50);
    int comp = lstrcmp(claName, className);
    free(claName);
    return comp == 0;
}

LRESULT WINAPI mozWndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC wndProc = mozWndProc;
    LONG_PTR wndProcWindow = GetWindowLongPtr(hWnd, wndProcAddress);
    if (wndProcWindow != NULL)
        wndProc = (WNDPROC)wndProcWindow;

    switch(msg) {
        case WM_CREATE:
            SetWindowLongPtr(hWnd, GWL_STYLE, GetWindowLongPtr(hWnd, GWL_STYLE) | WS_SYSMENU);
            break;
        case WM_STYLECHANGING:
            if ((int)wParam == GWL_STYLE) {
                STYLESTRUCT* style = (STYLESTRUCT*)lParam;

                // Append WS_SYSMENU to show the titlebar buttons
                if ((style->styleNew & WS_CAPTION) == WS_CAPTION) {
                    style->styleNew |= WS_SYSMENU;
                } else {
                    style->styleNew &= ~WS_SYSMENU;
                }
            }
            break;
        case WM_NCHITTEST:
        case WM_NCMOUSEMOVE:
        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONUP:
        case WM_NCLBUTTONDBLCLK:
            if (isMozWindowed(hWnd)) {
                POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (isCursorInControlsArea(hWnd, p)) {
                    LRESULT dwmResult;
                    
                    if (DwmDefWindowProc(hWnd, msg, wParam, lParam, &dwmResult))
                        return dwmResult;

                    dwmResult = DefWindowProc(hWnd, msg, wParam, lParam);
                    
                    // Because else the space near the buttons will not be draggable anymore
                    if (dwmResult == HTCLIENT)
                        dwmResult = HTCAPTION;

                    return dwmResult;
                }
            }
            break;
        case WM_NCCALCSIZE:
            if ((GetWindowLongPtr(hWnd, GWL_STYLE) & (WS_MAXIMIZE | WS_CAPTION)) == (WS_MAXIMIZE | WS_CAPTION)) {
                // clientRect->top needs to stay as 0 or else it breaks DwmDefWindowProc on maximized windows (thanks microsoft)

                RECT* clientRect =
                wParam ? &(reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam))->rgrc[0]
                    : (reinterpret_cast<RECT*>(lParam));


                int pad = GetSystemMetricsForDpi(SM_CYSIZEFRAME, GetDpiForWindow(hWnd)) * 2;
                clientRect->left += pad;
                clientRect->bottom -= pad;
                clientRect->right -= pad;

                return FALSE;
            }
    }
    return wndProc(hWnd, msg, wParam, lParam);
}

RegisterClassW_t RegisterClassW_Original;
ATOM WINAPI RegisterClassW_Hook(WNDCLASSW* cl) {
    if (lstrcmp(cl->lpszClassName, mozWindowClass) == 0) {
        Wh_Log(L"Registering MozillaWindowClass");
        // Firefox window!! QUICK!! HIJACK!!
        // The window procedure isn't hooked here already since it leads to a crash on latest Firefox
        mozWndProc = cl->lpfnWndProc;
        wndProcAddress = cl->cbWndExtra;
        cl->cbWndExtra += sizeof(LONG_PTR);
    }
    return RegisterClassW_Original(cl);
}

// Modifies the Window Procedure so that it keeps the new procedure sent by Firefox while still being hooked.
// Goal is to prioritize our WndProc hook.
LONG_PTR HookWndProc(HWND hWnd, int wndProcAddress, LONG_PTR newWndProc, LONG_PTR wndProcHook) {
    LONG_PTR p = GetWindowLongPtrW_Original(hWnd, wndProcAddress);
    if (p == NULL) p = GetWindowLongPtrW_Original(hWnd, GWLP_WNDPROC);
    if (newWndProc != wndProcHook)
        SetWindowLongPtrW_Original(hWnd, wndProcAddress, newWndProc);
    SetWindowLongPtrW_Original(hWnd, GWLP_WNDPROC, wndProcHook);
    return p;
}

LONG_PTR WINAPI SetWindowLongPtrW_Hook(HWND hWnd,int nIndex,LONG_PTR dwNewLong) {
    if (nIndex == GWLP_WNDPROC) {
        if (isOfWinClass(hWnd, mozWindowClass))
            return HookWndProc(hWnd, wndProcAddress, dwNewLong, (LONG_PTR)mozWndProcHook);
    }
    return SetWindowLongPtrW_Original(hWnd, nIndex, dwNewLong);
}

HRESULT DwmExtendFrameIntoClientArea_Hook(HWND hWnd, MARGINS *pMarInset) {
    if (isOfWinClass(hWnd, mozWindowClass)) {
        // Sets other borders to 0 so that it keeps that proper aero feel
        // This won't disrupt Vertical Tabs since the top value is not modified.
        pMarInset->cxLeftWidth = 0;
        pMarInset->cxRightWidth = 0;
        pMarInset->cyBottomHeight = 0;
        
        return DwmExtendFrameIntoClientArea_Original(hWnd, pMarInset);
    }
    return DwmExtendFrameIntoClientArea_Original(hWnd, pMarInset);
};

decltype(&CreateWindowExW) CreateWindowExW_Original;
HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle,LPCWSTR lpClassName,LPCWSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam) {
    HWND hWnd = CreateWindowExW_Original(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (isOfWinClass(hWnd, mozWindowClass)) {
        // Immediately hook our WndProc
        SetWindowLongPtrW_Original(hWnd, wndProcAddress, GetWindowLongPtrW_Original(hWnd, GWLP_WNDPROC));
        SetWindowLongPtrW_Original(hWnd, GWLP_WNDPROC, (LONG_PTR)mozWndProcHook);
    }
    return hWnd;
}

LONG_PTR WINAPI GetWindowLongPtrW_Hook(HWND hWnd,int nIndex) {
    if (nIndex == GWLP_WNDPROC && isOfWinClass(hWnd, mozWindowClass))
        return GetWindowLongPtrW_Original(hWnd, wndProcAddress);
    return GetWindowLongPtrW_Original(hWnd, nIndex);
}

BOOL Wh_ModInit() {
    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook, (void**)&CreateWindowExW_Original);
    Wh_SetFunctionHook((void*)RegisterClassW, (void*)RegisterClassW_Hook, (void**)&RegisterClassW_Original);
    Wh_SetFunctionHook((void*)GetWindowLongPtrW, (void*)GetWindowLongPtrW_Hook, (void**)&GetWindowLongPtrW_Original);
    Wh_SetFunctionHook((void*)SetWindowLongPtrW, (void*)SetWindowLongPtrW_Hook, (void**)&SetWindowLongPtrW_Original);

    HMODULE dwmAPI = LoadLibrary(L"dwmapi.dll");
    Wh_SetFunctionHook((void*)GetProcAddress(dwmAPI, "DwmExtendFrameIntoClientArea"), (void*)DwmExtendFrameIntoClientArea_Hook, (void**)&DwmExtendFrameIntoClientArea_Original);

    return TRUE;
}