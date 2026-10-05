#include "browser-client.hpp"
#include "base64/base64.hpp"
#include "json11/json11.hpp"

#include "SlBrowserWidget.h"

#include <QApplication>
#include <QThread>
#include <QToolTip>
#include "GrpcBrowser.h"
#include "JavascriptApi.h"
#include "SlBrowser.h"
#include "WindowsFunctions.h"

#include <json11/json11.hpp>

using namespace json11;

// True, with the reply filled in, while the tab's widget or browser is still being made
static bool replyIfNotReady(const std::shared_ptr<BrowserElements> &elements, std::string &jsonOutput)
{
	if (elements->ready && elements->widget && elements->browser)
		return false;

	jsonOutput = Json(Json::object({{"error", "not ready"}})).dump();
	return true;
}

bool BrowserClient::JS_BROWSER_RESIZE_BROWSER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int w = argsWithoutFunc[0]->GetInt();
	int h = argsWithoutFunc[1]->GetInt();

	if (w < 200 || h < 200 || w > 8096 || h > 8096)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	SlBrowser::instance().m_mainBrowser->widget->resize(w, h);

	return true;
}

bool BrowserClient::JS_BROWSER_BRING_FRONT(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	HWND hwnd = HWND(SlBrowser::instance().m_mainBrowser->widget->winId());

	if (::IsIconic(hwnd))
		::ShowWindow(hwnd, SW_RESTORE);

	WindowsFunctions::ForceForegroundWindow(hwnd);

	return true;
}

bool BrowserClient::JS_BROWSER_SET_WINDOW_POSITION(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t x = argsWithoutFunc[0]->GetInt();
	int32_t y = argsWithoutFunc[1]->GetInt();

	SlBrowser::instance().m_mainBrowser->widget->move(x, y);

	return true;
}

bool BrowserClient::JS_BROWSER_SET_ALLOW_HIDE_BROWSER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	SlBrowser::instance().m_allowHideBrowser = argsWithoutFunc[0]->GetBool();

	return true;
}

bool BrowserClient::JS_BROWSER_SET_HIDDEN_STATE(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	SlBrowser::instance().m_mainBrowser->widget->setHidden(argsWithoutFunc[0]->GetBool());
	SlBrowser::instance().saveHiddenState(SlBrowser::instance().m_mainBrowser->widget->isHidden());

	if (!SlBrowser::instance().m_mainBrowser->widget->isHidden())
	{
		HWND hwnd = HWND(SlBrowser::instance().m_mainBrowser->widget->winId());
		WindowsFunctions::ForceForegroundWindow(hwnd);
	}

	return true;
}

bool BrowserClient::JS_TABS_CREATE_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int uid = argsWithoutFunc[0]->GetInt();
	std::string url = argsWithoutFunc[1]->GetString();

	if (!SlBrowser::isApprovedTabUrl(url))
	{
		jsonOutput = Json(Json::object({{"error", "url is not allowed"}})).dump();
		return true;
	}

	TabWindowOptions options;
	options.title = "Streamlabs App Store";

	if (argsWithoutFunc.size() >= 3)
		options.title = argsWithoutFunc[2]->GetString();

	if (argsWithoutFunc.size() >= 4)
	{
		const std::string iconPath = argsWithoutFunc[3]->GetString();
		std::string err;

		if (!iconPath.empty())
			err = SlBrowser::instance().resolveTabIconPath(iconPath, options.iconPath);

		if (!err.empty())
		{
			jsonOutput = Json(Json::object({{"error", err}})).dump();
			return true;
		}
	}

	if (argsWithoutFunc.size() >= 5)
		options.initScript = argsWithoutFunc[4]->GetString();

	if (argsWithoutFunc.size() >= 6)
		options.hideOnClose = argsWithoutFunc[5]->GetBool();

	// The reply waits for the browser, so the caller can use the tab as soon as it hears back
	if (funcId != 0)
	{
		options.onCreated = [browser, funcId](const std::string &err) {
			CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("executeCallback");
			CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
			execute_args->SetInt(0, funcId);
			execute_args->SetString(1, err.empty() ? "{}" : Json(Json::object({{"error", err}})).dump());

			SendBrowserProcessMessage(browser, PID_RENDERER, msg);
		};
	}

	std::string err = SlBrowser::instance().createTabWindow(uid, url, std::move(options));

	if (!err.empty())
	{
		jsonOutput = Json(Json::object({{"error", err}})).dump();
		return true;
	}

	return false;
}

bool BrowserClient::JS_TABS_DESTROY_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	std::string err = SlBrowser::instance().queueDestroyCefBrowser(uid);

	if (!err.empty())
		jsonOutput = Json(Json::object({{"error", err}})).dump();

	return true;
}

bool BrowserClient::JS_TABS_LOAD_URL(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	std::string url = argsWithoutFunc[1]->GetString();

	if (!SlBrowser::isApprovedTabUrl(url))
	{
		jsonOutput = Json(Json::object({{"error", "url is not allowed"}})).dump();
		return true;
	}

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	if (replyIfNotReady(elementsPtr, jsonOutput))
		return true;

	if (auto mainFramePtr = elementsPtr->browser->GetMainFrame())
		mainFramePtr->LoadURL(url);

	return true;
}

bool BrowserClient::JS_TABS_RESIZE_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 3)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	int32_t w = argsWithoutFunc[1]->GetInt();
	int32_t h = argsWithoutFunc[2]->GetInt();

	if (w < 200 || h < 200 || w > 8096 || h > 8096)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	if (replyIfNotReady(elementsPtr, jsonOutput))
		return true;

	QMetaObject::invokeMethod(qApp, [elementsPtr, w, h]() { elementsPtr->widget->resize(w, h); }, Qt::QueuedConnection);

	return true;
}

bool BrowserClient::JS_TABS_HIDE_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	if (replyIfNotReady(elementsPtr, jsonOutput))
		return true;

	// Set now so a read right after this call sees it; the widget's events keep it right afterwards
	elementsPtr->hidden = true;

	QMetaObject::invokeMethod(qApp, [elementsPtr]() { elementsPtr->widget->hide(); }, Qt::QueuedConnection);

	return true;
}

bool BrowserClient::JS_TABS_SHOW_WINDOW(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	if (replyIfNotReady(elementsPtr, jsonOutput))
		return true;

	elementsPtr->hidden = false;

	QMetaObject::invokeMethod(
		qApp,
		[elementsPtr]() {
			elementsPtr->widget->show();

			HWND hwnd = HWND(elementsPtr->widget->winId());

			if (::IsIconic(hwnd))
				::ShowWindow(hwnd, SW_RESTORE);

			WindowsFunctions::ForceForegroundWindow(hwnd);
		},
		Qt::QueuedConnection);

	return true;
}

bool BrowserClient::JS_TABS_IS_WINDOW_HIDDEN(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	if (replyIfNotReady(elementsPtr, jsonOutput))
		return true;

	jsonOutput = Json(Json::object({{"result", elementsPtr->hidden.load()}})).dump();
	return true;
}

bool BrowserClient::JS_TABS_GET_WINDOW_CEF_IDENTIFIER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}
	int32_t uid = argsWithoutFunc[0]->GetInt();

	if (uid == 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t result = SlBrowser::instance().getBrowserCefId(uid);

	if (result <= 0)
	{
		const bool known = SlBrowser::instance().getBrowserElements(uid) != nullptr;
		jsonOutput = Json(Json::object({{"error", known ? "not ready" : "Invalid parameters"}})).dump();
		return true;
	}

	jsonOutput = Json(Json::object({{"result", result}})).dump();
	return true;
}

bool BrowserClient::JS_TABS_REGISTER_MSG_RECEIVER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	AssignMsgReceiverFunc(browser->GetIdentifier(), funcId);

	// No reply: the callback is only invoked with messages
	return false;
}

bool BrowserClient::JS_MAIN_REGISTER_MSG_RECEIVER_FROM_TABS(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	std::shared_ptr<BrowserElements> ptr = SlBrowser::instance().getBrowserElements(0);

	if (ptr == nullptr || ptr->browser == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "main not found"}})).dump();
		return true;
	}

	AssignMsgReceiverFunc(ptr->browser->GetIdentifier(), funcId);

	// No reply: the callback is only invoked with messages
	return false;
}

bool BrowserClient::JS_TAB_SEND_STRING_TO_MAIN(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	std::shared_ptr<BrowserElements> ptr = SlBrowser::instance().getBrowserElements(0);

	if (ptr == nullptr || ptr->browser == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "main not found"}})).dump();
		return true;
	}

	if (GetReceiverFuncIdForBrowser(ptr->browser->GetIdentifier()) == 0)
	{
		jsonOutput = Json(Json::object({{"error", "main has no receiver"}})).dump();
		return true;
	}

	SendMsgToReceiver(ptr->browser, argsWithoutFunc[0]->GetString(), SlBrowser::instance().getUuidFromCefId(browser->GetIdentifier()));
	return true;
}

bool BrowserClient::JS_MAIN_SEND_STRING_TO_TAB(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	if (uid == 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	std::shared_ptr<BrowserElements> ptr = SlBrowser::instance().getBrowserElements(uid);

	if (ptr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "uid not found"}})).dump();
		return true;
	}

	if (ptr->browser == nullptr || GetReceiverFuncIdForBrowser(ptr->browser->GetIdentifier()) == 0)
	{
		jsonOutput = Json(Json::object({{"error", "tab has no receiver"}})).dump();
		return true;
	}

	SendMsgToReceiver(ptr->browser, argsWithoutFunc[1]->GetString(), 0);
	return true;
}

bool BrowserClient::JS_TABS_SET_ICON(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	if (uid == 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	std::shared_ptr<BrowserElements> ptr = SlBrowser::instance().getBrowserElements(uid);

	if (ptr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "uid not found"}})).dump();
		return true;
	}

	std::wstring path;
	std::string err = SlBrowser::instance().resolveTabIconPath(argsWithoutFunc[1]->GetString(), path);

	if (!err.empty())
	{
		jsonOutput = Json(Json::object({{"error", err}})).dump();
		return true;
	}

	QWidget *mainWindow = SlBrowser::instance().m_mainBrowser->widget;

	QMetaObject::invokeMethod(
		mainWindow,
		[path, ptr]() {
			if (ptr->widget)
				ptr->widget->window()->setWindowIcon(QIcon(QString::fromStdWString(path)));
		},
		Qt::QueuedConnection);

	return true;
}

bool BrowserClient::JS_TABS_SET_TITLE(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	if (uid == 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	std::shared_ptr<BrowserElements> ptr = SlBrowser::instance().getBrowserElements(uid);

	if (ptr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "uid not found"}})).dump();
		return true;
	}

	std::string text = argsWithoutFunc[1]->GetString();

	QWidget *mainWindow = SlBrowser::instance().m_mainBrowser->widget;

	QMetaObject::invokeMethod(
		mainWindow,
		[text, ptr]() {
			if (ptr->widget)
				ptr->widget->window()->setWindowTitle(text.c_str());
		},
		Qt::QueuedConnection);

	return true;
}

bool BrowserClient::JS_TABS_EXECUTE_JS(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	if (uid == 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	std::shared_ptr<BrowserElements> ptr = SlBrowser::instance().getBrowserElements(uid);

	if (ptr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "uid not found"}})).dump();
		return true;
	}

	std::string code = argsWithoutFunc[1]->GetString();

	if (replyIfNotReady(ptr, jsonOutput))
		return true;

	if (auto fr = ptr->browser->GetMainFrame())
		fr->ExecuteJavaScript(code, fr->GetURL(), 0);

	return true;
}

bool BrowserClient::JS_TABS_QUERY_ALL(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	const std::map<int32_t, std::shared_ptr<BrowserElements>> &browsers = SlBrowser::instance().getExtraBrowsers();

	Json::array jsonArray;

	for (const auto &pair : browsers)
	{
		int32_t uid = pair.first;
		std::shared_ptr<BrowserElements> browserElement = pair.second;

		if (browserElement && browserElement->ready && browserElement->browser)
		{
			// Get the URL of the main frame of the browser
			if (auto fr = browserElement->browser->GetMainFrame())
			{
				std::string url = fr->GetURL();
				Json::object jsonObj = {{"uid", uid}, {"url", url}};
				jsonArray.push_back(jsonObj);
			}
		}
	}

	jsonOutput = Json(jsonArray).dump();
	return true;
}
