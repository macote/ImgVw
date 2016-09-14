#pragma once

#include "ImgBrowser.h"
#include "Window.h"
#include "TimestampLogger.h"
#include "resource.h"
#include <Windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <Gdiplus.h>
#include <string>
#include <vector>
#include <memory>

class ImgVwWindow : public Window
{
public:
	ImgVwWindow(HINSTANCE hinst, std::vector<std::wstring> args) : Window(hinst)
	{
#if _DEBUG && LOG1
		kLogger.WriteLine(L"Start");
#endif
		if (args.size() > 1)
		{
			path_ = args[1];
		}
	}
	~ImgVwWindow()
	{
#if _DEBUG && LOG1
		kLogger.WriteLine(L"End");
#endif
	}
	virtual LPCWSTR ClassName() { return L"ImgVwWindow"; }
	static ImgVwWindow* Create(HINSTANCE hInst, std::vector<std::wstring> args);
	HWND dlgcurrent() const { return dlgcurrent_; }
private:
	virtual void PaintContent(PAINTSTRUCT* pps);
	void MoveMouse();
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnCreate();
	void InitializeBrowser(std::wstring path);
	BOOL DisplayImage(HDC dc, ImgItem* item);
	void DisplayFileInformation(HDC dc, std::wstring filepath);
	void OnNCDestroy();
	static BOOL CALLBACK AboutDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);
private:
	ImgBrowser browser_;
	std::wstring path_;
	HWND dlgcurrent_{ NULL };
	HFONT captionfont_{ nullptr };
	LARGE_INTEGER frequency_{};
	BOOL mousemoved_{};
#if _DEBUG && LOG1
	static TimestampLogger kLogger;
	std::wstring FormatWindowMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
#endif
};
