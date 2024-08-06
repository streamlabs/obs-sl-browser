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

bool BrowserClient::JS_BROWSER_RESIZE_BROWSER(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
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

bool BrowserClient::JS_BROWSER_BRING_FRONT(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	HWND hwnd = HWND(SlBrowser::instance().m_mainBrowser->widget->winId());

	if (::IsIconic(hwnd))
		::ShowWindow(hwnd, SW_RESTORE);

	WindowsFunctions::ForceForegroundWindow(hwnd);

	return true;
}

bool BrowserClient::JS_BROWSER_SET_WINDOW_POSITION(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
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

bool BrowserClient::JS_BROWSER_SET_ALLOW_HIDE_BROWSER(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	SlBrowser::instance().m_allowHideBrowser = argsWithoutFunc[0]->GetBool();

	return true;
}

bool BrowserClient::JS_BROWSER_SET_HIDDEN_STATE(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
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

bool BrowserClient::JS_TABS_CREATE_WINDOW(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	browser->GetIdentifier();

	int uid = argsWithoutFunc[0]->GetInt();
	std::string url = argsWithoutFunc[1]->GetString();

	auto elements = std::make_shared<BrowserElements>();
	elements->widget = new SlBrowserWidget;
	elements->widget->setWindowTitle("Streamlabs App Store");
	elements->widget->setMinimumSize(320, 240);
	elements->widget->resize(1280, 720);
	elements->widget->showMinimized();

	SlBrowser::instance().createCefBrowser(uid, elements, url, false, false);

	std::string err = SlBrowser::instance().popLastError();

	if (!err.empty())
		jsonOutput = Json(Json::object({{"error", err}})).dump();

	return true;
}

bool BrowserClient::JS_TABS_DESTROY_WINDOW(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	SlBrowser::instance().queueDestroyCefBrowser(uid);

	std::string err = SlBrowser::instance().popLastError();

	if (!err.empty())
		jsonOutput = Json(Json::object({{"error", err}})).dump();

	return true;
}

bool BrowserClient::JS_TABS_LOAD_URL(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	std::string url = argsWithoutFunc[1]->GetString();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	auto browserPtr = elementsPtr->browser;

	if (auto mainFramePtr = browserPtr->GetMainFrame())
		mainFramePtr->LoadURL(url);

	return true;
}

bool BrowserClient::JS_TABS_RESIZE_WINDOW(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	if (argsWithoutFunc.size() < 3)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	int32_t w = argsWithoutFunc[1]->GetInt();
	int32_t h = argsWithoutFunc[1]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return true;
	}

	if (auto widget = elementsPtr->widget)
		widget->resize(w, h);

	return true;
}

bool BrowserClient::JS_TABS_HIDE_WINDOW(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
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

	if (auto widget = elementsPtr->widget)
		widget->hide();

	return true;
}

bool BrowserClient::JS_TABS_SHOW_WINDOW(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
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

	if (auto widget = elementsPtr->widget)
		widget->show();

	return true;
}

bool BrowserClient::JS_TABS_IS_WINDOW_HIDDEN(CefRefPtr<CefBrowser>& browser, int32_t& funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
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

	if (auto widget = elementsPtr->widget)
		jsonOutput = Json(Json::object({{"result", widget->isHidden()}})).dump();
	else
		jsonOutput = Json(Json::object({{"error", "Internal error, null ptr"}})).dump();

	return true;
}

bool BrowserClient::JS_TABS_GET_WINDOW_CEF_IDENTIFIER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	int32_t uid = argsWithoutFunc[0]->GetInt();

	if (uid == 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	int32_t result = SlBrowser::instance().getBrowserCefId(uid);

	if (result <= 0)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return true;
	}

	return true;
}

bool BrowserClient::JS_TABS_REGISTER_MSG_RECEIVER(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	AssignMsgReceiverFunc(browser->GetIdentifier(), funcId);
	return false;
}

bool BrowserClient::JS_MAIN_REGISTER_MSG_RECEIVER_FROM_TABS(CefRefPtr<CefBrowser> &browser, int32_t &funcId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput, std::string &internalMsgType)
{
	AssignMsgReceiverFunc(browser->GetIdentifier(), funcId);
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

	if (ptr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", "main not found"}})).dump();
		return true;
	}

	std::string msgStr = argsWithoutFunc[0]->GetString();

	AllocConsole();
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);

	printf("Was sending to %d\n", browser->GetIdentifier());

	browser = ptr->browser;
	internalMsgType = "executeCallback_NoDelete";
	funcId = GetReceiverFuncIdForBrowser(browser->GetIdentifier());
	jsonOutput = msgStr;

	printf("Now sending to %d\n", browser->GetIdentifier());
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

	std::string msgStr = argsWithoutFunc[1]->GetString();

	AllocConsole();
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);

	printf("Was sending to %d\n", browser->GetIdentifier());

	browser = ptr->browser;
	internalMsgType = "executeCallback_NoDelete";
	funcId = GetReceiverFuncIdForBrowser(browser->GetIdentifier());
	jsonOutput = msgStr;

	printf("Now sending to %d\n", browser->GetIdentifier());
	return true;
}
