#pragma once

#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>

#include "cef-headers.hpp"

typedef std::function<void(CefRefPtr<CefBrowser>)> BrowserFunc;

class BrowserApp : public CefApp, public CefRenderProcessHandler, public CefBrowserProcessHandler, public CefV8Handler
{
public:
	static BrowserApp& instance()
	{
		static BrowserApp app;
		return app;
	}

public:
	static std::string cefListValueToJSONString(CefRefPtr<CefListValue> listValue);
	bool PopCallback(const int32_t funcId, std::pair<CefRefPtr<CefV8Value>, CefRefPtr<CefV8Context>> &output);

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

private:
	int32_t m_callbackIdCounter = 0;
	std::map<int32_t, std::pair<CefRefPtr<CefV8Value>, CefRefPtr<CefV8Context>>> m_callbackMap;
	std::vector<std::pair<CefString, CefV8ValueList>> m_backlog;
	std::recursive_mutex m_executeMtx;
	bool m_connected = false;
};
