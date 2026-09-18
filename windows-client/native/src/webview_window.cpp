#include "hbox_client/webview_window.hpp"

#include <filesystem>

#include <wrl/event.h>

namespace hbox {
namespace {

std::wstring widenUtf8(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), length);
  return result;
}

}  // namespace

bool WebViewWindow::initialize(HWND window, const std::wstring& contentPath,
                               MessageHandler handler) {
  if (!window || !std::filesystem::is_directory(contentPath)) return false;
  window_ = window;
  handler_ = std::move(handler);

  const HRESULT scheduled = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, nullptr, nullptr,
      Microsoft::WRL::Callback<
          ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this, contentPath](HRESULT environmentResult,
                              ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(environmentResult) || !environment) {
              PostMessageW(window_, WM_APP + 2, 0,
                           static_cast<LPARAM>(environmentResult));
              return environmentResult;
            }
            return environment->CreateCoreWebView2Controller(
                window_,
                Microsoft::WRL::Callback<
                    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, contentPath](HRESULT controllerResult,
                                        ICoreWebView2Controller* controller)
                        -> HRESULT {
                      if (FAILED(controllerResult) || !controller) {
                        PostMessageW(window_, WM_APP + 2, 0,
                                     static_cast<LPARAM>(controllerResult));
                        return controllerResult;
                      }
                      controller_ = controller;
                      controller_->get_CoreWebView2(&webview_);
                      if (!webview_) return E_FAIL;

                      Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
                      HRESULT result = webview_.As(&webview3);
                      if (FAILED(result)) return result;
                      result = webview3->SetVirtualHostNameToFolderMapping(
                          L"app.hbox.local", contentPath.c_str(),
                          COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                      if (FAILED(result)) return result;

                      Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                      if (SUCCEEDED(webview_->get_Settings(&settings))) {
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_IsZoomControlEnabled(FALSE);
                      }

                      result = webview_->add_WebMessageReceived(
                          Microsoft::WRL::Callback<
                              ICoreWebView2WebMessageReceivedEventHandler>(
                              [this](ICoreWebView2*,
                                     ICoreWebView2WebMessageReceivedEventArgs*
                                         args) -> HRESULT {
                                LPWSTR raw = nullptr;
                                if (SUCCEEDED(args->TryGetWebMessageAsString(
                                        &raw)) && raw) {
                                  if (handler_) handler_(raw);
                                }
                                CoTaskMemFree(raw);
                                return S_OK;
                              })
                              .Get(),
                          &messageToken_);
                      hasMessageToken_ = SUCCEEDED(result);
                      resize();
                      return webview_->Navigate(
                          L"https://app.hbox.local/index.html");
                    })
                    .Get());
          })
          .Get());
  return SUCCEEDED(scheduled);
}

void WebViewWindow::resize() {
  if (!controller_ || !window_) return;
  RECT bounds{};
  GetClientRect(window_, &bounds);
  controller_->put_Bounds(bounds);
}

void WebViewWindow::postJson(const std::string& json) {
  if (!webview_) return;
  const auto wide = widenUtf8(json);
  if (!wide.empty()) (void)webview_->PostWebMessageAsJson(wide.c_str());
}

void WebViewWindow::shutdown() noexcept {
  handler_ = {};
  if (webview_ && hasMessageToken_) {
    (void)webview_->remove_WebMessageReceived(messageToken_);
  }
  hasMessageToken_ = false;
  webview_.Reset();
  if (controller_) (void)controller_->Close();
  controller_.Reset();
  window_ = nullptr;
}

}  // namespace hbox
