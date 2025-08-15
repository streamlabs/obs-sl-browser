#pragma once

#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <set>

#include "cef-headers.hpp"

typedef std::function<void(CefRefPtr<CefBrowser>)> BrowserFunc;

class BrowserApp : public CefApp, public CefRenderProcessHandler, public CefBrowserProcessHandler, public CefV8Handler
{
	bool m_isMainPluginWindow = false;
	int m_callbackIdCounter = 0;
	std::recursive_mutex m_callbackMutex;
	std::map<int, std::pair<CefRefPtr<CefV8Value>, CefRefPtr<CefV8Context>>> m_callbackMap;
	std::set<std::string> m_functionNames;

public:
	inline BrowserApp() {}

	virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
	virtual void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override;
	virtual void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
	virtual void OnBeforeCommandLineProcessing(const CefString &process_type, CefRefPtr<CefCommandLine> command_line) override;
	virtual void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
	virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;
	virtual bool Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) override;

	IMPLEMENT_REFCOUNTING(BrowserApp);
};
