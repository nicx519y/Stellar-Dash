#pragma once

#ifdef _WIN32

#include <windows.h>

#include <functional>
#include <string>

#include <wrl.h>
#include <WebView2.h>

namespace hbox {

class WebViewWindow {
 public:
  using MessageHandler = std::function<void(const std::wstring&)>;
  bool initialize(HWND window, const std::wstring& contentPath,
                  MessageHandler handler);
  void resize();
  void postJson(const std::string& json);
  void shutdown() noexcept;

 private:
  HWND window_{};
  MessageHandler handler_;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
  EventRegistrationToken messageToken_{};
  bool hasMessageToken_{};
};

}  // namespace hbox

#endif
