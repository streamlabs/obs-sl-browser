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

private:
	void UpdateExtraTexture();
	bool valid() const;

	bool m_reroute_audio = true;

	CefRefPtr<CefBrowser> m_Browser;
};
