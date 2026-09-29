#include <windows.h>

#include <dbt.h>
#include <powrprof.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include "hbox_client/client_runtime.hpp"
#include "hbox_client/webview_window.hpp"

namespace {

constexpr wchar_t kWindowClass[] = L"HBoxClientWindow";
constexpr wchar_t kWindowTitle[] = L"HBox Client";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kWebViewFailed = WM_APP + 2;
constexpr UINT_PTR kSnapshotTimer = 1;
constexpr UINT kTrayOpen = 1001;
constexpr UINT kTrayExit = 1002;

struct Application {
  HWND window{};
  HDEVNOTIFY deviceNotification{};
  NOTIFYICONDATAW tray{};
  bool exiting{};
  hbox::ClientRuntime runtime;
  hbox::WebViewWindow webview;
  std::string lastStateSignature;
};

std::wstring executablePath() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                          static_cast<DWORD>(path.size()));
  path.resize(length);
  return path;
}

bool containsArgument(const wchar_t* requested) {
  int count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) return false;
  bool found = false;
  for (int index = 1; index < count; ++index) {
    if (_wcsicmp(arguments[index], requested) == 0) {
      found = true;
      break;
    }
  }
  LocalFree(arguments);
  return found;
}

bool setAutostart(bool enabled) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return false;
  }
  LONG result;
  if (enabled) {
    const auto command = L"\"" + executablePath() + L"\" --background";
    result = RegSetValueExW(
        key, L"HBox Client", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    result = RegDeleteValueW(key, L"HBox Client");
    if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

void showWindow(Application& app) {
  ShowWindow(app.window, SW_SHOW);
  SetForegroundWindow(app.window);
}

void addTrayIcon(Application& app) {
  app.tray.cbSize = sizeof(app.tray);
  app.tray.hWnd = app.window;
  app.tray.uID = 1;
  app.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  app.tray.uCallbackMessage = kTrayMessage;
  app.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(app.tray.szTip, L"HBox Client");
  Shell_NotifyIconW(NIM_ADD, &app.tray);
  app.tray.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &app.tray);
}

void showTrayMenu(Application& app) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kTrayOpen, L"打开 HBox Client");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayExit, L"退出并释放设备");
  POINT point{};
  GetCursorPos(&point);
  SetForegroundWindow(app.window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                 point.x, point.y, 0, app.window, nullptr);
  DestroyMenu(menu);
}

void postSnapshot(Application& app, bool stateChanged) {
  const auto payload = app.runtime.snapshotJson();
  app.webview.postJson(std::string("{\"event\":\"runtime.snapshot\",\"payload\":") +
                       payload + "}");
  if (stateChanged) {
    app.webview.postJson(
        std::string("{\"event\":\"runtime.stateChanged\",\"payload\":") +
        payload + "}");
  }
}

void handleWebMessage(Application& app, const std::wstring& message) {
  if (message == L"{\"command\":\"runtime.getSnapshot\"}") {
    postSnapshot(app, false);
    return;
  }
  if (message == L"{\"command\":\"runtime.setHighPerformanceEnabled\",\"enabled\":true}" ||
      message == L"{\"command\":\"runtime.setHighPerformanceEnabled\",\"enabled\":false}") {
    const bool enabled = message.ends_with(L"true}");
    app.runtime.setHighPerformanceEnabled(enabled);
    postSnapshot(app, true);
    return;
  }
  if (message == L"{\"command\":\"window.hide\"}") {
    ShowWindow(app.window, SW_HIDE);
    return;
  }
  if (message == L"{\"command\":\"app.exit\"}") {
    app.exiting = true;
    DestroyWindow(app.window);
  }
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam) {
  auto* app = reinterpret_cast<Application*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    app = static_cast<Application*>(create->lpCreateParams);
    app->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(app));
  }
  if (!app) return DefWindowProcW(window, message, wparam, lparam);

  switch (message) {
    case WM_SIZE:
      app->webview.resize();
      return 0;
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      return 0;
    case WM_DEVICECHANGE:
      app->runtime.notifyDevicesChanged();
      return 0;
    case WM_POWERBROADCAST:
      if (wparam == PBT_APMSUSPEND) app->runtime.prepareForSuspend();
      if (wparam == PBT_APMRESUMEAUTOMATIC ||
          wparam == PBT_APMRESUMESUSPEND) {
        app->runtime.resumeFromSuspend();
      }
      return TRUE;
    case WM_TIMER: {
      if (wparam != kSnapshotTimer) break;
      const auto snapshot = app->runtime.snapshot();
      const std::string signature =
          std::string(hbox::runtimeModeName(snapshot.mode)) + ':' +
          (snapshot.connected ? "1" : "0") + ':' +
          (snapshot.virtualConnected ? "1" : "0") + ':' +
          (snapshot.highPerformanceEnabled ? "1" : "0") + ':' +
          snapshot.errorCode;
      const bool changed = signature != app->lastStateSignature;
      app->lastStateSignature = signature;
      postSnapshot(*app, changed);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wparam) == kTrayOpen) showWindow(*app);
      if (LOWORD(wparam) == kTrayExit) {
        app->exiting = true;
        DestroyWindow(window);
      }
      return 0;
    case kTrayMessage:
      if (LOWORD(lparam) == WM_LBUTTONUP ||
          LOWORD(lparam) == NIN_SELECT ||
          LOWORD(lparam) == NIN_KEYSELECT) {
        showWindow(*app);
      } else if (LOWORD(lparam) == WM_RBUTTONUP ||
                 LOWORD(lparam) == WM_CONTEXTMENU) {
        showTrayMenu(*app);
      }
      return 0;
    case kWebViewFailed:
      MessageBoxW(window,
                  L"WebView2 初始化失败。请确认 Microsoft Edge WebView2 "
                  L"Runtime 已安装。",
                  kWindowTitle, MB_ICONERROR | MB_OK);
      return 0;
    case WM_DESTROY:
      KillTimer(window, kSnapshotTimer);
      if (app->deviceNotification) {
        UnregisterDeviceNotification(app->deviceNotification);
        app->deviceNotification = nullptr;
      }
      Shell_NotifyIconW(NIM_DELETE, &app->tray);
      app->webview.shutdown();
      app->runtime.stop();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\HBoxClient.V1");
  if (!singleInstance || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (singleInstance) CloseHandle(singleInstance);
    return 0;
  }
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
    CloseHandle(singleInstance);
    return 1;
  }

  if (containsArgument(L"--install-autostart")) {
    const bool ok = setAutostart(true);
    CoUninitialize();
    CloseHandle(singleInstance);
    return ok ? 0 : 2;
  }
  if (containsArgument(L"--remove-autostart")) {
    const bool ok = setAutostart(false);
    CoUninitialize();
    CloseHandle(singleInstance);
    return ok ? 0 : 2;
  }

  Application app;
  WNDCLASSEXW windowClass{sizeof(windowClass)};
  windowClass.lpfnWndProc = windowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&windowClass)) {
    CoUninitialize();
    CloseHandle(singleInstance);
    return 1;
  }

  HWND window = CreateWindowExW(
      0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr,
      instance, &app);
  if (!window) {
    CoUninitialize();
    CloseHandle(singleInstance);
    return 1;
  }

  DEV_BROADCAST_DEVICEINTERFACE_W filter{};
  filter.dbcc_size = sizeof(filter);
  filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  app.deviceNotification = RegisterDeviceNotificationW(
      window, &filter,
      DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
  addTrayIcon(app);
  app.runtime.start();

  std::filesystem::path contentPath =
      std::filesystem::path(executablePath()).parent_path() / L"ui";
  if (!std::filesystem::is_directory(contentPath)) {
    contentPath = std::filesystem::path(HBOX_UI_DIST_DIR);
  }
  if (!app.webview.initialize(
          window, contentPath.wstring(),
          [&app](const std::wstring& message) {
            handleWebMessage(app, message);
          })) {
    MessageBoxW(window,
                L"找不到 UI 静态资源。请先构建 windows-client/ui。",
                kWindowTitle, MB_ICONERROR | MB_OK);
  }
  SetTimer(window, kSnapshotTimer, 20, nullptr);
  if (!containsArgument(L"--background")) {
    ShowWindow(window, showCommand == 0 ? SW_SHOW : showCommand);
    UpdateWindow(window);
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  CoUninitialize();
  ReleaseMutex(singleInstance);
  CloseHandle(singleInstance);
  return static_cast<int>(message.wParam);
}
