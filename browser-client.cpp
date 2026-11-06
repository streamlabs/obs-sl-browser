#include "browser-client.hpp"
#include "base64/base64.hpp"
#include "json11/json11.hpp"

#include <QApplication>
#include <QThread>
#include <QToolTip>
#include "GrpcBrowser.h"
#include "JavascriptApi.h"
#include "SlBrowser.h"
#include "WindowsFunctions.h"
#include "DiagLog.h"

#include <include/base/cef_callback.h>
#include <include/wrapper/cef_closure_task.h>
#include <json11/json11.hpp>

using namespace json11;

inline bool BrowserClient::valid() const
{
	return true;
}

std::string BrowserClient::diagTag(CefRefPtr<CefBrowser> browser) const
{
	std::string tag = m_isMain ? "main" : "tab";

	if (!m_isMain)
	{
		auto elements = m_elements.lock();
		tag += " uid=" + (elements ? std::to_string(elements->uid) : std::string("?"));
	}

	return tag + " cef=" + (browser ? std::to_string(browser->GetIdentifier()) : std::string("?"));
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

bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect)
{
	// Only a tab's own page is held to the approved origins; its subframes and main's pages are not
	if (m_isMain || !frame->IsMain())
		return false;

	const std::string url = request->GetURL();
	const bool blocked = !SlBrowser::isApprovedTabUrl(url);

	SL_DIAG << "browser: OnBeforeBrowse " << diagTag(browser) << " url=" << SlDiag::url(url) << " from=" << SlDiag::url(frame->GetURL()) << " transition=0x" << std::hex << request->GetTransitionType() << std::dec << " user_gesture=" << user_gesture << " is_redirect=" << is_redirect
		<< (blocked ? " BLOCKED" : "");

	return blocked;
}

CefResourceRequestHandler::ReturnValue BrowserClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, CefRefPtr<CefCallback>)
{
	return RV_CONTINUE;
}
#endif

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
	std::string initScript;
	if (auto elements = m_elements.lock())
		initScript = elements->initScript;

	SL_DIAG << "browser: OnAfterCreated " << diagTag(browser) << " url=" << SlDiag::url(browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString() : std::string()) << " initScript " << (initScript.empty() ? std::string("none") : SlDiag::script(initScript));
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &url)
{
	if (!m_isMain && frame->IsMain())
		SL_DIAG << "browser: OnAddressChange " << diagTag(browser) << " url=" << SlDiag::url(url);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser)
{
	SL_DIAG << "browser: DoClose " << diagTag(browser);

	if (!m_isMain)
	{
		if (auto elements = m_elements.lock(); elements && !elements->closed)
		{
			// A page requested the close. Handle it before CEF sends WM_CLOSE to Qt's
			// root window, whose closeEvent ignores the event. The queued cleanup
			// closes the detached CEF child after the uid is removed.
			if (SlBrowser::instance().closeTabWindow(elements->uid, elements))
				return true;
		}
	}

	return false;
}

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
#if CHROME_VERSION_BUILD >= 6834
				  int,
#endif
				  const CefString &target_url, const CefString &, cef_window_open_disposition_t disposition, bool user_gesture, const CefPopupFeatures &, CefWindowInfo &, CefRefPtr<CefClient> &, CefBrowserSettings &, CefRefPtr<CefDictionaryValue> &, bool *)
{
	SL_DIAG << "browser: OnBeforePopup BLOCKED " << diagTag(browser) << " target=" << SlDiag::url(target_url) << " disposition=" << disposition << " user_gesture=" << user_gesture;

	/* block popups */
	return true;
}

void BrowserClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model)
{
	/* remove all context menu contributions */
	model->Clear();
}

/*static*/
json11::Json convertCefValueToJSON(CefRefPtr<CefValue> value)
{
	switch (value->GetType())
	{
	case VTYPE_NULL:
		return nullptr;

	case VTYPE_BOOL:
		return value->GetBool();

	case VTYPE_INT:
		return value->GetInt();

	case VTYPE_DOUBLE:
		return value->GetDouble();

	case VTYPE_STRING:
		return value->GetString().ToString();

	case VTYPE_LIST: {
		const auto &list = value->GetList();
		std::vector<json11::Json> jsonList;
		for (size_t i = 0; i < list->GetSize(); ++i)
		{
			jsonList.push_back(convertCefValueToJSON(list->GetValue(i)));
		}
		return jsonList;
	}

	case VTYPE_DICTIONARY: {
		const auto &dict = value->GetDictionary();
		std::map<std::string, json11::Json> jsonMap;
		CefDictionaryValue::KeyList keys;
		dict->GetKeys(keys);
		for (const auto &key : keys)
		{
			jsonMap[key] = convertCefValueToJSON(dict->GetValue(key));
		}
		return jsonMap;
	}

	default:
		return nullptr;
	}
}

/*static*/
std::string BrowserClient::cefListValueToJSONString(CefRefPtr<CefListValue> listValue)
{
	std::map<std::string, json11::Json> jsonMap;
	for (size_t i = 0; i < listValue->GetSize(); ++i)
	{
		// Convert index to string key like "param1", "param2", ...
		std::string key = "param" + std::to_string(i + 1);
		jsonMap[key] = convertCefValueToJSON(listValue->GetValue(i));
	}

	std::string json_str;
	json_str = json11::Json(jsonMap).dump();
	return json_str;
}

CefRefPtr<CefBrowser> BrowserClient::GetMostRecentRenderKnown()
{
	std::lock_guard<std::recursive_mutex> grd(m_recursiveMutex);
	return m_MostRecentRenderKnowOf;
}

void BrowserClient::RegisterCallback(const int functionId, CefRefPtr<CefBrowser> browser)
{
	std::lock_guard<std::recursive_mutex> grd(m_recursiveMutex);
	m_callbackDictionary[functionId] = browser;
	m_MostRecentRenderKnowOf = browser;
}

void BrowserClient::RemoveBrowserFromCallback(CefRefPtr<CefBrowser> browser)
{
	std::lock_guard<std::recursive_mutex> grd(m_recursiveMutex);

	if (m_MostRecentRenderKnowOf != nullptr && m_MostRecentRenderKnowOf->GetIdentifier() == browser->GetIdentifier())
		m_MostRecentRenderKnowOf = nullptr;

	for (auto itr = m_callbackDictionary.begin(); itr != m_callbackDictionary.end();)
	{
		if (itr->second->GetIdentifier() == browser->GetIdentifier())
			itr = m_callbackDictionary.erase(itr);
		else
			++itr;
	}

	std::lock_guard<std::mutex> receiverGrd(m_tabReceiverMutex);
	m_tabReceiverDictionary.erase(browser->GetIdentifier());

	std::lock_guard<std::mutex> eventGrd(m_obsEventReceiverMutex);

	if (m_obsEventReceiverBrowserId == browser->GetIdentifier())
		m_obsEventReceiver = {};
}

CefRefPtr<CefBrowser> BrowserClient::PopCallback(const int functionId)
{
	std::lock_guard<std::recursive_mutex> grd(m_recursiveMutex);

	auto itr = m_callbackDictionary.find(functionId);

	if (itr != m_callbackDictionary.end())
	{
		CefRefPtr<CefBrowser> browser = itr->second;
		m_callbackDictionary.erase(itr);
		return browser;
	}

	return nullptr;
}

std::mutex BrowserClient::m_tabReceiverMutex;
std::map<int32_t, BrowserClient::Receiver> BrowserClient::m_tabReceiverDictionary;

void BrowserClient::AssignMsgReceiverFunc(const int32_t browserCefId, const int32_t funcid, const std::string &token)
{
	std::lock_guard<std::mutex> grd(m_tabReceiverMutex);
	m_tabReceiverDictionary[browserCefId] = {funcid, token};
}

BrowserClient::Receiver BrowserClient::GetMsgReceiver(const int32_t browserCefId)
{
	std::lock_guard<std::mutex> grd(m_tabReceiverMutex);
	auto itr = m_tabReceiverDictionary.find(browserCefId);
	return itr != m_tabReceiverDictionary.end() ? itr->second : Receiver{};
}

// 0 when the browser has no receiver
int32_t BrowserClient::GetReceiverFuncIdForBrowser(const int32_t browserCefId)
{
	return GetMsgReceiver(browserCefId).funcId;
}

std::mutex BrowserClient::m_obsEventReceiverMutex;
int32_t BrowserClient::m_obsEventReceiverBrowserId = 0;
BrowserClient::Receiver BrowserClient::m_obsEventReceiver;
std::mutex BrowserClient::m_obsEventPendingMutex;
std::deque<std::string> BrowserClient::m_obsEventPending;
bool BrowserClient::m_obsEventDrainScheduled = false;
bool BrowserClient::m_obsEventOverflowed = false;

namespace
{
constexpr size_t kMaxPendingObsEvents = 10000;
constexpr int kObsEventsPerTask = 64;
}

void BrowserClient::AssignObsEventReceiverFunc(const int32_t browserCefId, const int32_t funcid, const std::string &token)
{
	std::lock_guard<std::mutex> grd(m_obsEventReceiverMutex);
	m_obsEventReceiverBrowserId = browserCefId;
	m_obsEventReceiver = {funcid, token};
}

void BrowserClient::SendObsEventToReceiver(const std::string &json)
{
	// Receiver registration also runs on CEF UI. Read its ID and send on that
	// thread so the replacement ACK cannot overtake an event using the old ID.
	if (!CefCurrentlyOn(TID_UI))
	{
		bool schedule = false;
		{
			std::lock_guard<std::mutex> grd(m_obsEventPendingMutex);
			if (m_obsEventPending.size() >= kMaxPendingObsEvents)
			{
				if (!m_obsEventOverflowed)
					printf("sl-proxy: OBS event queue is full, dropping events until CEF catches up\n");
				m_obsEventOverflowed = true;
				return;
			}
			m_obsEventPending.push_back(json);
			if (!m_obsEventDrainScheduled)
			{
				m_obsEventDrainScheduled = true;
				schedule = true;
			}
		}
		if (schedule && !CefPostTask(TID_UI, base::BindOnce(&DrainObsEvents)))
		{
			std::lock_guard<std::mutex> grd(m_obsEventPendingMutex);
			m_obsEventPending.clear();
			m_obsEventDrainScheduled = false;
			m_obsEventOverflowed = false;
		}
		return;
	}

	int32_t browserId = 0;
	Receiver receiver;

	{
		std::lock_guard<std::mutex> grd(m_obsEventReceiverMutex);
		browserId = m_obsEventReceiverBrowserId;
		receiver = m_obsEventReceiver;
	}

	if (receiver.funcId == 0)
		return;

	const auto main = SlBrowser::instance().m_mainBrowser;

	if (main == nullptr || main->browser == nullptr || main->browser->GetIdentifier() != browserId)
		return;

	CefRefPtr<CefProcessMessage> processMsg = CefProcessMessage::Create("executeCallback_NoDelete");
	CefRefPtr<CefListValue> execute_args = processMsg->GetArgumentList();
	execute_args->SetInt(0, receiver.funcId);
	execute_args->SetString(1, json);
	execute_args->SetInt(2, 0);
	execute_args->SetString(3, receiver.token);

	SendBrowserProcessMessage(main->browser, PID_RENDERER, processMsg);
}

void BrowserClient::DrainObsEvents()
{
	for (int i = 0; i < kObsEventsPerTask; ++i)
	{
		std::string json;
		{
			std::lock_guard<std::mutex> grd(m_obsEventPendingMutex);
			if (m_obsEventPending.empty())
			{
				m_obsEventDrainScheduled = false;
				m_obsEventOverflowed = false;
				return;
			}
			json = std::move(m_obsEventPending.front());
			m_obsEventPending.pop_front();
		}
		SendObsEventToReceiver(json);
	}

	if (!CefPostTask(TID_UI, base::BindOnce(&DrainObsEvents)))
	{
		std::lock_guard<std::mutex> grd(m_obsEventPendingMutex);
		m_obsEventPending.clear();
		m_obsEventDrainScheduled = false;
		m_obsEventOverflowed = false;
	}
}

void BrowserClient::SendMsgToReceiver(CefRefPtr<CefBrowser> target, const std::string &msg, const int32_t senderUid)
{
	if (target == nullptr)
		return;

	const Receiver receiver = GetMsgReceiver(target->GetIdentifier());

	if (receiver.funcId == 0)
		return;

	CefRefPtr<CefProcessMessage> processMsg = CefProcessMessage::Create("executeCallback_NoDelete");
	CefRefPtr<CefListValue> execute_args = processMsg->GetArgumentList();
	execute_args->SetInt(0, receiver.funcId);
	execute_args->SetString(1, msg);
	execute_args->SetInt(2, senderUid);
	execute_args->SetString(3, receiver.token);

	SendBrowserProcessMessage(target, PID_RENDERER, processMsg);
}

bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, CefRefPtr<CefProcessMessage> message)
{
	const std::string &name = message->GetName();
	CefRefPtr<CefListValue> input_args = message->GetArgumentList();
	if (name == "releaseReceiver" && processId == PID_RENDERER)
	{
		const int id = input_args->GetInt(0);
		const std::string token = input_args->GetString(1).ToString();
		const int browserId = browser->GetIdentifier();
		SL_DIAG << "browser: releaseReceiver " << diagTag(browser) << " funcId=" << id << " token=" << token.substr(0, 8);
		{
			std::lock_guard<std::mutex> grd(m_tabReceiverMutex);
			auto itr = m_tabReceiverDictionary.find(browserId);
			if (itr != m_tabReceiverDictionary.end() && itr->second.funcId == id && itr->second.token == token)
				m_tabReceiverDictionary.erase(itr);
		}
		{
			std::lock_guard<std::mutex> grd(m_obsEventReceiverMutex);
			if (m_obsEventReceiverBrowserId == browserId && m_obsEventReceiver.funcId == id && m_obsEventReceiver.token == token)
				m_obsEventReceiver = {};
		}
		return true;
	}

	if (!valid())
		return false;

	int funcid = input_args->GetInt(0);

	// The renderer only binds what its role allows, but a compromised one can send any name
	const bool allowedForRole = m_isMain ? (JavascriptApi::isPluginFunctionName(name) || JavascriptApi::isBrowserFunctionName(name)) : JavascriptApi::isBrowserTabFunctionName(name);
	const std::string frameUrl = frame->GetURL();
	const bool allowedForFrame = m_isMain ? (frame->IsMain() && !SlBrowser::getApiOrigin().empty() && BrowserApp::OriginOf(frameUrl) == SlBrowser::getApiOrigin()) : (frame->IsMain() && SlBrowser::isApprovedTabUrl(frameUrl));

	if (!allowedForFrame || !allowedForRole)
	{
		printf("Refused api call %s from %s\n", name.c_str(), frameUrl.c_str());
		SL_DIAG << "browser: REFUSED api call " << name << " " << diagTag(browser) << " frameMain=" << frame->IsMain() << " frame=" << SlDiag::url(frameUrl);

		if (funcid != 0)
		{
			CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("executeCallback");
			CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
			execute_args->SetInt(0, funcid);
			execute_args->SetString(1, Json(Json::object({{"error", "Not allowed"}})).dump());

			frame->SendProcessMessage(PID_RENDERER, msg);
		}

		return true;
	}

	if (JavascriptApi::isBrowserFunctionName(name) || JavascriptApi::isBrowserTabFunctionName(name))
	{
		std::string jsonOutput = "{}";

		std::vector<CefRefPtr<CefValue>> argsWithoutFunc;

		for (size_t l = 1; l < input_args->GetSize(); l++)
			argsWithoutFunc.push_back(input_args->GetValue(l));

		bool retVal = true;
		std::string internalMsgType = "executeCallback";

		const auto functionId = JavascriptApi::getFunctionId(name);

		// Message traffic is too chatty; everything else that touches tabs is traced
		if (name.rfind("tab", 0) == 0 && functionId != JavascriptApi::JS_TAB_SEND_STRING_TO_MAIN && functionId != JavascriptApi::JS_MAIN_SEND_STRING_TO_TAB)
			SL_DIAG << "browser: api " << name << " from " << diagTag(browser) << " funcId=" << funcid << " frame=" << SlDiag::url(frameUrl);
		switch (functionId)
		{
		case JavascriptApi::JS_MAIN_SEND_STRING_TO_TAB:
			retVal = JS_MAIN_SEND_STRING_TO_TAB(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_MAIN_REGISTER_MSG_RECEIVER_FROM_TABS:
			retVal = JS_MAIN_REGISTER_MSG_RECEIVER_FROM_TABS(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_REGISTER_MSG_RECEIVER:
			retVal = JS_TABS_REGISTER_MSG_RECEIVER(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TAB_SEND_STRING_TO_MAIN:
			retVal = JS_TAB_SEND_STRING_TO_MAIN(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_BROWSER_RESIZE_BROWSER:
			retVal = JS_BROWSER_RESIZE_BROWSER(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_BROWSER_BRING_FRONT:
			retVal = JS_BROWSER_BRING_FRONT(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_BROWSER_SET_WINDOW_POSITION:
			retVal = JS_BROWSER_SET_WINDOW_POSITION(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_BROWSER_SET_ALLOW_HIDE_BROWSER:
			retVal = JS_BROWSER_SET_ALLOW_HIDE_BROWSER(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_BROWSER_SET_HIDDEN_STATE:
			retVal = JS_BROWSER_SET_HIDDEN_STATE(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_CREATE_WINDOW:
			retVal = JS_TABS_CREATE_WINDOW(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_DESTROY_WINDOW:
			retVal = JS_TABS_DESTROY_WINDOW(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_RESIZE_WINDOW:
			retVal = JS_TABS_RESIZE_WINDOW(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_LOAD_URL:
			retVal = JS_TABS_LOAD_URL(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_GET_WINDOW_CEF_IDENTIFIER:
			retVal = JS_TABS_GET_WINDOW_CEF_IDENTIFIER(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_EXECUTE_JS:
			retVal = JS_TABS_EXECUTE_JS(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_QUERY_ALL:
			retVal = JS_TABS_QUERY_ALL(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_SHOW_WINDOW:
			retVal = JS_TABS_SHOW_WINDOW(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_HIDE_WINDOW:
			retVal = JS_TABS_HIDE_WINDOW(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_IS_WINDOW_HIDDEN:
			retVal = JS_TABS_IS_WINDOW_HIDDEN(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_SET_ICON:
			retVal = JS_TABS_SET_ICON(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_TABS_SET_TITLE:
			retVal = JS_TABS_SET_TITLE(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		case JavascriptApi::JS_EVENTS_REGISTER_RECEIVER:
			retVal = JS_EVENTS_REGISTER_RECEIVER(browser, funcid, argsWithoutFunc, jsonOutput, internalMsgType);
			break;
		default:
			jsonOutput = Json(Json::object({{"error", "Unknown function"}})).dump();
			break;
		}

		if (!retVal && funcid > 0 &&
		    (functionId == JavascriptApi::JS_MAIN_REGISTER_MSG_RECEIVER_FROM_TABS || functionId == JavascriptApi::JS_TABS_REGISTER_MSG_RECEIVER || functionId == JavascriptApi::JS_EVENTS_REGISTER_RECEIVER))
		{
			// The renderer keeps the previous receiver until this registration is active.
			CefRefPtr<CefProcessMessage> ack = CefProcessMessage::Create("receiverRegistered");
			ack->GetArgumentList()->SetInt(0, funcid);
			ack->GetArgumentList()->SetString(1, input_args->GetString(1));
			frame->SendProcessMessage(PID_RENDERER, ack);
		}

		if (retVal)
		{
			CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create(internalMsgType);
			CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
			execute_args->SetInt(0, funcid);
			execute_args->SetString(1, jsonOutput);

			SendBrowserProcessMessage(browser, PID_RENDERER, msg);
		}
	}
	else
	{
		RegisterCallback(funcid, browser);

		if (!GrpcBrowser::instance().getClient()->send_js_api(name, cefListValueToJSONString(input_args)))
		{
			// todo; handle
			abort();
			return false;
		}
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

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser)
{
	SL_DIAG << "browser: OnBeforeClose " << diagTag(browser);
	RemoveBrowserFromCallback(browser);

	// If CEF closes a tab outside the normal cleanup path, unregister that exact tab.
	if (!m_isMain)
	{
		if (auto elements = m_elements.lock())
			SlBrowser::instance().closeTabWindow(elements->uid, elements);
	}

	auto onBeforeClose = std::move(m_onBeforeClose);
	m_onBeforeClose = nullptr;

	if (onBeforeClose)
		onBeforeClose();
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
	if (frame->IsMain())
		SL_DIAG << "browser: OnLoadStart " << diagTag(browser) << " url=" << SlDiag::url(frame->GetURL()) << " transition=0x" << std::hex << transition_type << std::dec;

	if (m_isMain && frame->IsMain())
		SlBrowser::instance().setMainLoadingInProgress(true);
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode)
{
	if (frame->IsMain())
		SL_DIAG << "browser: OnLoadEnd " << diagTag(browser) << " url=" << SlDiag::url(frame->GetURL()) << " status=" << httpStatusCode;

	if (!m_isMain || !frame->IsMain())
		return;

	if (httpStatusCode == 200)
		SlBrowser::instance().setMainPageSuccess(true);

	SlBrowser::instance().setMainLoadingInProgress(false);
}

void BrowserClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code, const CefString &error_string)
{
	SL_DIAG << "browser: OnRenderProcessTerminated " << diagTag(browser) << " status=" << status << " error_code=" << error_code << " error=" << error_string.ToString();
	RemoveBrowserFromCallback(browser);
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl)
{
	if (frame->IsMain())
		SL_DIAG << "browser: OnLoadError " << diagTag(browser) << " url=" << SlDiag::url(failedUrl) << " code=" << errorCode << " text=" << errorText.ToString();
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString &message, const CefString &source, int line)
{
	if (m_isMain || level < LOGSEVERITY_WARNING)
		return false;

	// Replaces CEF's own console line for tabs, which would carry app tokens unredacted
	SL_DIAG << "console " << diagTag(browser) << " level=" << level << " " << SlDiag::url(source) << ":" << line << " " << SlDiag::redactTokens(message.ToString()).substr(0, 1500);
	return true;
}
