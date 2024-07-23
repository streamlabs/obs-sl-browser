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

	int x = argsWithoutFunc[0]->GetInt();
	int y = argsWithoutFunc[1]->GetInt();

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

void BrowserClient::JS_CREATE_APP_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{

}

void BrowserClient::JS_DESTROY_APP_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{

}

void BrowserClient::JS_RESIZE_APP_WINDOW(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{

}

void BrowserClient::JS_LOAD_APP_URL(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId processId, const std::vector<CefRefPtr<CefValue>> &argsWithoutFunc, std::string &jsonOutput)
{

}
