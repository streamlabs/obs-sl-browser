#include "browser-client.hpp"
#include "base64/base64.hpp"
#include "json11/json11.hpp"

#include <QApplication>
#include <QThread>
#include <QToolTip>
#include "GrpcBrowserWindow.h"
#include "JavascriptApi.h"
#include "SlBrowser.h"
#include "WindowsFunctions.h"

#include <json11/json11.hpp>

using namespace json11;

inline bool BrowserClient::valid() const
{
	return true;
}

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler()
{
	return this;
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler()
{
	return this;
}

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler()
{
	return this;
}

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler()
{
	return this;
}

CefRefPtr<CefContextMenuHandler> BrowserClient::GetContextMenuHandler()
{
	return this;
}

CefRefPtr<CefAudioHandler> BrowserClient::GetAudioHandler()
{
	return m_reroute_audio ? this : nullptr;
}

#if CHROME_VERSION_BUILD >= 4638
CefRefPtr<CefRequestHandler> BrowserClient::GetRequestHandler()
{
	return this;
}

CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, bool, bool, const CefString &, bool &)
{
	if (request->GetHeaderByName("origin") == "null")
	{
		return this;
	}

	return nullptr;
}

CefResourceRequestHandler::ReturnValue BrowserClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, CefRefPtr<CefCallback>)
{
	return RV_CONTINUE;
}
#endif

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &, const CefString &, cef_window_open_disposition_t, bool, const CefPopupFeatures &, CefWindowInfo &, CefRefPtr<CefClient> &, CefBrowserSettings &, CefRefPtr<CefDictionaryValue> &, bool *)
{
	/* block popups */
	return true;
}

void BrowserClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model)
{
	/* remove all context menu contributions */
	model->Clear();
}

bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefProcessId processId, CefRefPtr<CefProcessMessage> message)
{
	const std::string &name = message->GetName();
	CefRefPtr<CefListValue> input_args = message->GetArgumentList();

	if (!valid())
		return false;

	if (name == "OnContextCreated")
	{
		// Tell the proxy process the port that the plugin is listening on
		CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("GrpcPort");
		CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
		execute_args->SetInt(0, SlBrowser::instance().m_pluginListenPort);
		SendBrowserProcessMessage(browser, PID_RENDERER, msg);
		return true;
	}

	if (JavascriptApi::isBrowserFunctionName(name))
	{
		int funcid = input_args->GetInt(0);

		std::string jsonOutput = "{}";

		std::vector <CefRefPtr<CefValue>> argsWithoutFunc;

		for (u_long l = 1; l < input_args->GetSize(); l++)
			argsWithoutFunc.push_back(input_args->GetValue(l));

		// Stuff done right here and now to the browser
		// Put this into a sub function if it gets bigger
		switch (JavascriptApi::getFunctionId(name))
		{
		case JavascriptApi::JS_QT_RESIZE_BROWSER:
		{
			if (argsWithoutFunc.size() < 2)
			{
				jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
				break;
			}

			int w = argsWithoutFunc[0]->GetInt();
			int h = argsWithoutFunc[1]->GetInt();

			if (w < 200 || h < 200 || w > 8096 || h > 8096)
			{
				jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
				break;
			}

			SlBrowser::instance().m_widget->resize(w, h);
			break;
		}
		case JavascriptApi::JS_QT_BRING_FRONT:
		{
			HWND hwnd = HWND(SlBrowser::instance().m_widget->winId());

			if (::IsIconic(hwnd))
				::ShowWindow(hwnd, SW_RESTORE);

			WindowsFunctions::ForceForegroundWindow(hwnd);
			break;
		}
		case JavascriptApi::JS_QT_SET_WINDOW_POSITION:
		{
			if (argsWithoutFunc.size() < 2)
			{
				jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
				break;
			}

			int x = argsWithoutFunc[0]->GetInt();
			int y = argsWithoutFunc[1]->GetInt();

			SlBrowser::instance().m_widget->move(x, y);
			break;
		}
		case JavascriptApi::JS_QT_SET_ALLOW_HIDE_BROWSER:
		{
			if (argsWithoutFunc.size() < 1)
			{
				jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
				break;
			}

			SlBrowser::instance().m_allowHideBrowser = argsWithoutFunc[0]->GetBool();
			break;
		}
		}

		CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("BrowserWindowCallback");
		CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
		execute_args->SetInt(0, funcid);
		execute_args->SetString(1, jsonOutput);

		SendBrowserProcessMessage(browser, PID_RENDERER, msg);
	}


	return true;
}

void BrowserClient::GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect)
{
	if (!valid())
	{
		rect.Set(0, 0, 16, 16);
		return;
	}

	rect.Set(0, 0, 1920, 1080);
}

bool BrowserClient::OnTooltip(CefRefPtr<CefBrowser>, CefString &text)
{
	std::string str_text = text;
	QMetaObject::invokeMethod(QCoreApplication::instance()->thread(), [str_text]() { QToolTip::showText(QCursor::pos(), str_text.c_str()); });
	return true;
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList &, const void *buffer, int width, int height)
{
	if (type != PET_VIEW)
	{
		// TODO Overlay texture on top of bs->texture
		return;
	}

	if (!valid())
	{
		return;
	}
}

void BrowserClient::OnAudioStreamStarted(CefRefPtr<CefBrowser> browser, const CefAudioParameters &params_, int channels_)
{
	m_channels = channels_;
	m_channel_layout = (ChannelLayout)params_.channel_layout;
	m_sample_rate = params_.sample_rate;
	m_frames_per_buffer = params_.frames_per_buffer;
}

void BrowserClient::OnAudioStreamPacket(CefRefPtr<CefBrowser> browser, const float **data, int frames, int64_t pts)
{
	if (!valid())
	{
		return;
	}
}

void BrowserClient::OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) {}

void BrowserClient::OnAudioStreamError(CefRefPtr<CefBrowser> browser, const CefString &message) {}

static CefAudioHandler::ChannelLayout Convert2CEFSpeakerLayout(int channels)
{
	switch (channels)
	{
	case 1:
		return CEF_CHANNEL_LAYOUT_MONO;
	case 2:
		return CEF_CHANNEL_LAYOUT_STEREO;
	case 3:
		return CEF_CHANNEL_LAYOUT_2_1;
	case 4:
		return CEF_CHANNEL_LAYOUT_4_0;
	case 5:
		return CEF_CHANNEL_LAYOUT_4_1;
	case 6:
		return CEF_CHANNEL_LAYOUT_5_1;
	case 8:
		return CEF_CHANNEL_LAYOUT_7_1;
	default:
		return CEF_CHANNEL_LAYOUT_UNSUPPORTED;
	}
}

bool BrowserClient::GetAudioParameters(CefRefPtr<CefBrowser> browser, CefAudioParameters &params)
{
	int channels = 2;
	params.channel_layout = CEF_CHANNEL_LAYOUT_MONO;
	params.sample_rate = 48000;
	params.frames_per_buffer = kFramesPerBuffer;
	return true;
}

void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transition_type)
{
	SlBrowser::instance().setMainLoadingInProgress(true);
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int httpStatusCode)
{
	if (httpStatusCode == 200)
		SlBrowser::instance().setMainPageSuccess(true);

	SlBrowser::instance().setMainLoadingInProgress(false);
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl)
{

}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t level, const CefString &message, const CefString &source, int line)
{
	return false;
}
