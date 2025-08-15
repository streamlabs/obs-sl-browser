#pragma once

#include "cef-headers.hpp"

#include <map>
#include <mutex>

struct BrowserSource;

class BrowserClient : public CefClient, public CefDisplayHandler, public CefLifeSpanHandler, public CefRequestHandler, public CefResourceRequestHandler, public CefContextMenuHandler, public CefRenderHandler, public CefAudioHandler, public CefLoadHandler
{

public:
	inline BrowserClient(bool reroute_audio_) : m_reroute_audio(reroute_audio_) {}

	/* CefClient */
	CefRefPtr<CefLoadHandler> GetLoadHandler() override;
	CefRefPtr<CefRenderHandler> GetRenderHandler() override;
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
	CefRefPtr<CefRequestHandler> GetRequestHandler() override;

	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override;
	CefRefPtr<CefAudioHandler> GetAudioHandler() override;

	/* CefDisplayHandler */
	bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString &message, const CefString &source, int line) override;

	/* CefLifeSpanHandler */
	bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &target_url, const CefString &target_frame_name, cef_window_open_disposition_t target_disposition, bool user_gesture, const CefPopupFeatures &popupFeatures, CefWindowInfo &windowInfo,
			   CefRefPtr<CefClient> &client, CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extra_info, bool *no_javascript_access) override;

	bool OnTooltip(CefRefPtr<CefBrowser> browser, CefString &text) override;
	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;

	/* CefRequestHandler */
	CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool is_navigation, bool is_download, const CefString &request_initiator, bool &disable_default_handling) override;

	/* CefResourceRequestHandler */
	CefResourceRequestHandler::ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) override;

	/* CefContextMenuHandler */
	void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) override;

	/* CefRenderHandler */
	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override;

	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) override;
	void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser, const float **data, int frames, int64_t pts) override;
	void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override;
	void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser, const CefAudioParameters &params, int channels) override;
	void OnAudioStreamError(CefRefPtr<CefBrowser> browser, const CefString &message) override;

	const int kFramesPerBuffer = 1024;
	virtual bool GetAudioParameters(CefRefPtr<CefBrowser> browser, CefAudioParameters &params) override;

	/* CefLoadHandler */
	void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type) override;
	void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
	void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl) override;

	int m_channels = 0;
	int m_sample_rate = 0;
	int m_frames_per_buffer = 0;

	ChannelLayout m_channel_layout;

	IMPLEMENT_REFCOUNTING(BrowserClient);

public:
	CefRefPtr<CefBrowser> GetMostRecentRenderKnown();
	CefRefPtr<CefBrowser> PopCallback(const int functionId);
	void RegisterCallback(const int functionId, CefRefPtr<CefBrowser> browser);
	void RemoveBrowserFromCallback(CefRefPtr<CefBrowser> browser);

public:
	static std::string cefListValueToJSONString(CefRefPtr<CefListValue> listValue);

private:
	void UpdateExtraTexture();
	void AssignMsgReceiverFunc(const int32_t browserCefId, const int32_t funcid);

	bool valid() const;

	int32_t GetReceiverFuncIdForBrowser(const int32_t browserCefId);

	int32_t m_msgReceiverFuncId = 0;
	bool m_reroute_audio = true;
	std::recursive_mutex m_recursiveMutex;
	std::map<int32_t, CefRefPtr<CefBrowser>> m_callbackDictionary;

	CefRefPtr<CefBrowser> m_MostRecentRenderKnowOf = nullptr;

	std::mutex m_mutex;
	static std::map<int32_t, int32_t> m_tabReceiverDictionary;

private:
	bool JS_BROWSER_RESIZE_BROWSER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_BROWSER_BRING_FRONT(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_BROWSER_SET_WINDOW_POSITION(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_BROWSER_SET_ALLOW_HIDE_BROWSER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_BROWSER_SET_HIDDEN_STATE(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_CREATE_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_DESTROY_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_RESIZE_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_LOAD_URL(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_HIDE_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_SHOW_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_IS_WINDOW_HIDDEN(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_REGISTER_MSG_RECEIVER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TAB_SEND_STRING_TO_MAIN(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_MAIN_REGISTER_MSG_RECEIVER_FROM_TABS(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_GET_WINDOW_CEF_IDENTIFIER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_MAIN_SEND_STRING_TO_TAB(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_EXECUTE_JS(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_QUERY_ALL(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_SET_ICON(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
	bool JS_TABS_SET_TITLE(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType);
};
