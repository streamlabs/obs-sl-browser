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

void BrowserClient::JS_BROWSER_RESIZE_BROWSER(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int w = argsWithoutFunc[0]->GetInt();
	int h = argsWithoutFunc[1]->GetInt();

	if (w < 200 || h < 200 || w > 8096 || h > 8096)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	SlBrowser::instance().m_mainBrowser->widget->resize(w, h);
}

void BrowserClient::JS_BROWSER_BRING_FRONT(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	HWND hwnd = HWND(SlBrowser::instance().m_mainBrowser->widget->winId());

	if (::IsIconic(hwnd))
		::ShowWindow(hwnd, SW_RESTORE);

	WindowsFunctions::ForceForegroundWindow(hwnd);
}

void BrowserClient::JS_BROWSER_SET_WINDOW_POSITION(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t x = argsWithoutFunc[0]->GetInt();
	int32_t y = argsWithoutFunc[1]->GetInt();

	SlBrowser::instance().m_mainBrowser->widget->move(x, y);
}

void BrowserClient::JS_BROWSER_SET_ALLOW_HIDE_BROWSER(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	SlBrowser::instance().m_allowHideBrowser = argsWithoutFunc[0]->GetBool();
}

void BrowserClient::JS_BROWSER_SET_HIDDEN_STATE(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	SlBrowser::instance().m_mainBrowser->widget->setHidden(argsWithoutFunc[0]->GetBool());
	SlBrowser::instance().saveHiddenState(SlBrowser::instance().m_mainBrowser->widget->isHidden());

	if (!SlBrowser::instance().m_mainBrowser->widget->isHidden())
	{
		HWND hwnd = HWND(SlBrowser::instance().m_mainBrowser->widget->winId());
		WindowsFunctions::ForceForegroundWindow(hwnd);
	}
}

void BrowserClient::JS_TABS_CREATE_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

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
}

void BrowserClient::JS_TABS_DESTROY_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	SlBrowser::instance().queueDestroyCefBrowser(uid);

	std::string err = SlBrowser::instance().popLastError();

	if (!err.empty())
		jsonOutput = Json(Json::object({{"error", err}})).dump();
}

void BrowserClient::JS_TABS_LOAD_URL(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 2)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	std::string url = argsWithoutFunc[1]->GetString();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return;
	}

	auto browserPtr = elementsPtr->browser;

	if (auto mainFramePtr = browserPtr->GetMainFrame())
		mainFramePtr->LoadURL(url);
}

void BrowserClient::JS_TABS_RESIZE_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 3)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();
	int32_t w = argsWithoutFunc[1]->GetInt();
	int32_t h = argsWithoutFunc[1]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return;
	}

	if (auto widget = elementsPtr->widget)
		widget->resize(w, h);
}

void BrowserClient::JS_TABS_HIDE_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return;
	}

	if (auto widget = elementsPtr->widget)
		widget->hide();
}

void BrowserClient::JS_TABS_SHOW_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return;
	}

	if (auto widget = elementsPtr->widget)
		widget->show();
}

void BrowserClient::JS_TABS_IS_WINDOW_HIDDEN(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{
	if (argsWithoutFunc.size() < 1)
	{
		jsonOutput = Json(Json::object({{"error", "Invalid parameters"}})).dump();
		return;
	}

	int32_t uid = argsWithoutFunc[0]->GetInt();

	auto elementsPtr = SlBrowser::instance().getBrowserElements(uid);

	if (elementsPtr == nullptr)
	{
		jsonOutput = Json(Json::object({{"error", SlBrowser::instance().popLastError() + ". Did not find " + std::to_string(uid)}})).dump();
		return;
	}

	if (auto widget = elementsPtr->widget)
		jsonOutput = Json(Json::object({{"result", widget->isHidden()}})).dump();
	else
		jsonOutput = Json(Json::object({{"error", "Internal error, null ptr"}})).dump();
}
