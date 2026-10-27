#pragma once

#include <map>
#include <unordered_map>
#include <functional>
#include <mutex>

#include "cef-headers.hpp"

typedef std::function<void(CefRefPtr<CefBrowser>)> BrowserFunc;

class BrowserApp : public CefApp, public CefRenderProcessHandler, public CefBrowserProcessHandler, public CefV8Handler
{
	int m_callbackIdCounter = 0;
	std::recursive_mutex m_callbackMutex;
	enum class ReceiverKind
	{
		None,
		Messages,
		ObsEvents,
	};

	struct CallbackInfo
	{
		CefRefPtr<CefV8Value> function;
		CefRefPtr<CefV8Context> context;
		int browserId;
		ReceiverKind receiverKind;
	};

	std::map<int, CallbackInfo> m_callbackMap;

	// Told by the browser process when it creates the browser, so a browser it did not describe binds nothing
	enum class Role
	{
		None,
		Main,
		Tab,
	};

	Role roleOf(CefRefPtr<CefBrowser> browser);

	// A browser can be created again in this process before its previous incarnation is destroyed, as a navigation does, so it is only forgotten when the last one is gone
	struct BrowserInfo
	{
		int instances = 0;
		Role role = Role::None;
		std::string initScript;
	};

	std::mutex m_browserInfoMutex;
	std::map<int, BrowserInfo> m_browserInfos;
	std::string m_apiOrigin;

public:
	inline BrowserApp(const std::string &apiOrigin = {}) : m_apiOrigin(apiOrigin) {}

	static std::string OriginOf(const std::string &url);
	static bool isApprovedTabUrl(const std::string &url);

	// Port from SL_PLUGIN_REMOTE_DEBUGGING_PORT, or 0 when unset or invalid.
	static int getRemoteDebuggingPort();

	virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override;
	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
	virtual void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override;
	virtual void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
	virtual void OnBeforeCommandLineProcessing(const CefString &process_type, CefRefPtr<CefCommandLine> command_line) override;
	virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> extra_info) override;
	virtual void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override;
	virtual bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command_line, const CefString &current_directory) override;
	virtual void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
	virtual void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
	virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;
	virtual bool Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) override;

	IMPLEMENT_REFCOUNTING(BrowserApp);
};
