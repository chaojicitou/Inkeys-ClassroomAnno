#include "AnnoCore.h"
#include <cstdio>
#include <cstring>

extern void CloseProgram();

namespace anno
{
	Config cfg;

	static HWND g_trayWnd = nullptr;
	static HICON g_trayIcon = nullptr;
	static bool g_trayAdded = false;
	static bool g_quitting = false;

	static const UINT WM_TRAY = WM_APP + 10;

	// ---------------- 配置 ----------------
	static std::wstring ExeDir()
	{
		wchar_t buf[MAX_PATH] = { 0 };
		GetModuleFileNameW(nullptr, buf, MAX_PATH);
		std::wstring s(buf);
		size_t p = s.find_last_of(L'\\');
		return p == std::wstring::npos ? s : s.substr(0, p);
	}

	static std::wstring ConfigPath()
	{
		std::wstring d = ExeDir();
		CreateDirectoryW((d + L"\\opt").c_str(), nullptr);
		return d + L"\\opt\\Anno.ini";
	}

	void LoadConfig()
	{
		std::wstring p = ConfigPath();
		const wchar_t* f = p.c_str();
		cfg.penColor = (int)GetPrivateProfileIntW(L"pen", L"color", 0, f);
		cfg.penWidth = (int)GetPrivateProfileIntW(L"pen", L"width", 2, f);
		cfg.penAlpha = (int)GetPrivateProfileIntW(L"pen", L"alpha", 0, f);
		cfg.eraserSize = (int)GetPrivateProfileIntW(L"eraser", L"size", 1, f);
		cfg.multiFinger = GetPrivateProfileIntW(L"opt", L"multiFinger", 1, f) != 0;
		cfg.rightClickSelect = GetPrivateProfileIntW(L"opt", L"rightClickSelect", 1, f) != 0;
		cfg.toolbarCollapsed = GetPrivateProfileIntW(L"opt", L"collapsed", 0, f) != 0;
		cfg.autoStart = GetPrivateProfileIntW(L"opt", L"autoStart", 1, f) != 0;
		cfg.leftX = (int)GetPrivateProfileIntW(L"bar", L"leftX", (UINT)-1, f);
		cfg.leftY = (int)GetPrivateProfileIntW(L"bar", L"leftY", (UINT)-1, f);
		cfg.rightX = (int)GetPrivateProfileIntW(L"bar", L"rightX", (UINT)-1, f);
		cfg.rightY = (int)GetPrivateProfileIntW(L"bar", L"rightY", (UINT)-1, f);

		if (cfg.penColor < 0 || cfg.penColor >= 9) cfg.penColor = 0;
		if (cfg.penWidth < 0 || cfg.penWidth >= 6) cfg.penWidth = 2;
		if (cfg.penAlpha < 0 || cfg.penAlpha >= 4) cfg.penAlpha = 0;
		if (cfg.eraserSize < 0 || cfg.eraserSize > 3) cfg.eraserSize = 1;
	}

	void SaveConfig()
	{
		std::wstring p = ConfigPath();
		const wchar_t* f = p.c_str();
		wchar_t buf[64];

		swprintf_s(buf, L"%d", cfg.penColor); WritePrivateProfileStringW(L"pen", L"color", buf, f);
		swprintf_s(buf, L"%d", cfg.penWidth); WritePrivateProfileStringW(L"pen", L"width", buf, f);
		swprintf_s(buf, L"%d", cfg.penAlpha); WritePrivateProfileStringW(L"pen", L"alpha", buf, f);
		swprintf_s(buf, L"%d", cfg.eraserSize); WritePrivateProfileStringW(L"eraser", L"size", buf, f);
		WritePrivateProfileStringW(L"opt", L"multiFinger", cfg.multiFinger ? L"1" : L"0", f);
		WritePrivateProfileStringW(L"opt", L"rightClickSelect", cfg.rightClickSelect ? L"1" : L"0", f);
		WritePrivateProfileStringW(L"opt", L"collapsed", cfg.toolbarCollapsed ? L"1" : L"0", f);
		WritePrivateProfileStringW(L"opt", L"autoStart", cfg.autoStart ? L"1" : L"0", f);
		swprintf_s(buf, L"%d", cfg.leftX); WritePrivateProfileStringW(L"bar", L"leftX", buf, f);
		swprintf_s(buf, L"%d", cfg.leftY); WritePrivateProfileStringW(L"bar", L"leftY", buf, f);
		swprintf_s(buf, L"%d", cfg.rightX); WritePrivateProfileStringW(L"bar", L"rightX", buf, f);
		swprintf_s(buf, L"%d", cfg.rightY); WritePrivateProfileStringW(L"bar", L"rightY", buf, f);
	}

	static void BootLog(const char* text);

	void AnnoBootLog(const char* tag, long long v)
	{
		char buf[160];
		sprintf_s(buf, "%s=%lld", tag, v);
		BootLog(buf);
	}

	// ---------------- 开机自启 ----------------
	static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	static const wchar_t* kRunName = L"$Inkeys";

	static void BootLog(const char* text)
	{
		std::wstring p = ExeDir() + L"\\anno_boot.log";
		HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) return;
		SetFilePointer(h, 0, nullptr, FILE_END);
		DWORD written = 0;
		WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
		WriteFile(h, "\r\n", 2, &written, nullptr);
		CloseHandle(h);
	}

	static bool QueryAutoStart()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
		wchar_t value[MAX_PATH] = { 0 };
		DWORD type = 0, size = sizeof(value);
		LONG ret = RegQueryValueExW(hKey, kRunName, nullptr, &type, (LPBYTE)value, &size);
		RegCloseKey(hKey);
		if (ret != ERROR_SUCCESS || type != REG_SZ) return false;
		return value[0] != 0;
	}

	static void ApplyAutoStart(bool on)
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
		if (on)
		{
			wchar_t path[MAX_PATH] = { 0 };
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
			RegSetValueExW(hKey, kRunName, 0, REG_SZ, (const BYTE*)quoted.c_str(),
				(DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
		}
		else
		{
			RegDeleteValueW(hKey, kRunName);
		}
		RegCloseKey(hKey);
	}

	// ---------------- 托盘菜单 ----------------
	static void ShowTrayMenu()
	{
		HMENU menu = CreatePopupMenu();
		AppendMenuW(menu, MF_STRING, 1, AnnoVisible() ? L"隐藏批注层" : L"显示批注层");
		AppendMenuW(menu, MF_STRING, 2, L"展开 / 收起工具栏");
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING | (cfg.multiFinger ? MF_CHECKED : 0), 3, L"多指唤出橡皮");
		AppendMenuW(menu, MF_STRING | (cfg.rightClickSelect ? MF_CHECKED : 0), 4, L"右击切换选择");
		AppendMenuW(menu, MF_STRING | (cfg.autoStart ? MF_CHECKED : 0), 5, L"开机自启");
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING, 6, L"清屏");
		AppendMenuW(menu, MF_STRING, 7, L"退出智绘教");

		POINT pt;
		GetCursorPos(&pt);
		SetForegroundWindow(g_trayWnd);
		int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_trayWnd, nullptr);
		DestroyMenu(menu);
		PostMessageW(g_trayWnd, WM_NULL, 0, 0);

		switch (cmd)
		{
		case 1: AnnoToggleVisible(); break;
		case 2: UI_SetToolbarCollapsed(!cfg.toolbarCollapsed); break;
		case 3: cfg.multiFinger = !cfg.multiFinger; SaveConfig(); break;
		case 4: cfg.rightClickSelect = !cfg.rightClickSelect; SaveConfig(); break;
		case 5:
			cfg.autoStart = !cfg.autoStart;
			ApplyAutoStart(cfg.autoStart);
			SaveConfig();
			break;
		case 6:
			doc.Clear();
			doc.ClearSelection();
			g_quitting = false;
			UI_Invalidate();
			break;
		case 7:
			CloseProgram();
			break;
		default:
			break;
		}
	}

	static LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_TRAY:
			if (lParam == WM_LBUTTONUP) AnnoToggleVisible();
			else if (lParam == WM_RBUTTONUP) ShowTrayMenu();
			return 0;
		case WM_HOTKEY:
			if (wParam == 1) AnnoToggleVisible();
			else if (wParam == 2) UI_SetToolbarCollapsed(!cfg.toolbarCollapsed);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	void AnnoRegisterHotkeys(HWND hwnd)
	{
		RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'A');
		RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B');
	}

	// ---------------- 线程入口 ----------------
	void AnnoMain()
	{
		GdiplusStartupInput gdiInput;
		ULONG_PTR gdiToken = 0;
		GdiplusStartup(&gdiToken, &gdiInput, nullptr);

		LoadConfig();
		{
			bool run = QueryAutoStart();
			if (cfg.autoStart && !run) { ApplyAutoStart(true); run = true; }
			cfg.autoStart = run;
		}

		UI_Create();

		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = TrayProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"AnnoTrayWnd";
		RegisterClassExW(&wc);

		g_trayWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"AnnoTrayWnd", L"AnnoTray", WS_POPUP,
			0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

		if (g_trayWnd)
		{
			AnnoRegisterHotkeys(g_trayWnd);

			g_trayIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(191), IMAGE_ICON,
				0, 0, LR_DEFAULTSIZE | LR_SHARED);
			if (!g_trayIcon) g_trayIcon = LoadIconW(nullptr, IDI_APPLICATION);

			NOTIFYICONDATAW nid{};
			nid.cbSize = sizeof(nid);
			nid.hWnd = g_trayWnd;
			nid.uID = 1;
			nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
			nid.uCallbackMessage = WM_TRAY;
			nid.hIcon = g_trayIcon;
			wcscpy_s(nid.szTip, L"课堂批注（智绘教）");
			if (Shell_NotifyIconW(NIM_ADD, &nid)) g_trayAdded = true;
		}

		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (g_trayAdded)
		{
			NOTIFYICONDATAW nid{};
			nid.cbSize = sizeof(nid);
			nid.hWnd = g_trayWnd;
			nid.uID = 1;
			Shell_NotifyIconW(NIM_DELETE, &nid);
			g_trayAdded = false;
		}
		UI_Destroy();
		GdiplusShutdown(gdiToken);
	}
}
