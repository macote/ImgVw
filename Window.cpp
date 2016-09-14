#include "Window.h"

void Window::Register()
{
	WNDCLASSEX wndclassex;
	wndclassex.cbSize = sizeof(WNDCLASSEX);
	wndclassex.style = CS_HREDRAW | CS_VREDRAW;
	wndclassex.lpfnWndProc = Window::WndProc;
	wndclassex.cbClsExtra = 0;
	wndclassex.cbWndExtra = 0;
	wndclassex.hInstance = hinst_;
	wndclassex.hIcon = (HICON)LoadImage(hinst_, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
	wndclassex.hIconSm = (HICON)LoadImage(hinst_, MAKEINTRESOURCE(IDI_SMALL), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	wndclassex.hCursor = manualcursor_ ? NULL : LoadCursor(NULL, IDC_ARROW);
	wndclassex.hbrBackground = dontfillbackground_ ? NULL : backgroundbrush_ != nullptr ? backgroundbrush_ : GetSysColorBrush(COLOR_BTNFACE);
	wndclassex.lpszMenuName = NULL;
	wndclassex.lpszClassName = ClassName();
	WinRegisterClass(&wndclassex);
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Window *self;
	if (uMsg == WM_NCCREATE)
	{
		auto lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
		self = reinterpret_cast<Window*>(lpcs->lpCreateParams);
		self->hwnd_ = hwnd;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(self));
	}
	else
	{
		self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	}

	if (self)
	{
		return self->HandleMessage(uMsg, wParam, lParam);
	}
	else
	{
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

LRESULT Window::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT lres;
	switch (uMsg)
	{
	case WM_NCDESTROY:
		lres = DefWindowProc(hwnd_, uMsg, wParam, lParam);
		SetWindowLongPtr(hwnd_, GWLP_USERDATA, 0);
		delete this;
		return lres;
	case WM_PAINT:
		OnPaint();
		return 0;
	case WM_PRINTCLIENT:
		OnPrintClient(reinterpret_cast<HDC>(wParam));
		return 0;
	}

	return DefWindowProc(hwnd_, uMsg, wParam, lParam);
}

void Window::OnPaint()
{
	PAINTSTRUCT ps;
	BeginPaint(hwnd_, &ps);
	PaintContent(&ps);
	EndPaint(hwnd_, &ps);
}

void Window::OnPrintClient(HDC hdc)
{
	PAINTSTRUCT ps;
	ps.hdc = hdc;
	GetClientRect(hwnd_, &ps.rcPaint);
	PaintContent(&ps);
}