#pragma once

#include "resource.h"
#include <Windows.h>

class Window
{
public:
    Window(HINSTANCE hinst) : hinst_(hinst) { }
    virtual ~Window() { }
    HWND hwnd() const { return hwnd_; }
protected:
    HINSTANCE hinst_;
    HWND hwnd_{ NULL };
    HBRUSH backgroundbrush_{ nullptr };
    BOOL manualcursor_{};
    BOOL dontfillbackground_{};
protected:
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void PaintContent(PAINTSTRUCT* pps) = 0;
    virtual LPCWSTR ClassName() const = 0;
    virtual BOOL WinRegisterClass(WNDCLASSEX* pwc)
    {
        return RegisterClassEx(pwc);
    }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    HWND WinCreateWindow(DWORD dwExStyle, LPCWSTR pszName, DWORD dwStyle,
        int x, int y, int cx, int cy, HWND hwndParent, HMENU hmenu)
    {
        Register();

        return CreateWindowEx(dwExStyle, ClassName(), pszName, dwStyle,
            x, y, cx, cy, hwndParent, hmenu, hinst_, this);
    }
private:
    void Register();
    void OnPaint();
    void OnPrintClient(HDC hdc);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};