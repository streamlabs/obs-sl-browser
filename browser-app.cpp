#include <winsock2.h>
#include "browser-app.hpp"
#include "browser-version.h"
#include "JavascriptApi.h"

#include <json11/json11.hpp>

#include <windows.h>

#include <algorithm>

using namespace json11;

/*static*/
int BrowserApp::getRemoteDebuggingPort()
{
	char buffer[32];
	DWORD len = GetEnvironmentVariableA("SL_PLUGIN_REMOTE_DEBUGGING_PORT", buffer, sizeof(buffer));

	if (len == 0 || len >= sizeof(buffer))
		return 0;

	char *end = nullptr;
	long port = strtol(buffer, &end, 10);

	if (*end != '\0' || port < 1024 || port > 65535)
		return 0;

	return static_cast<int>(port);
}

CefRefPtr<CefRenderProcessHandler> BrowserApp::GetRenderProcessHandler()
{
	return this;
}

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler()
{
	return this;
}

void BrowserApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {}

void BrowserApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line)
{
	std::string pid = std::to_string(GetCurrentProcessId());
	command_line->AppendSwitchWithValue("parent_pid", pid);

	if (!m_apiOrigin.empty())
		command_line->AppendSwitchWithValue("api_origin", m_apiOrigin);
}

std::string BrowserApp::OriginOf(const std::string &url)
{
	CefURLParts parts;
	if (!CefParseURL(url, parts))
		return {};

	std::string scheme = CefString(&parts.scheme).ToString();
	std::string host = CefString(&parts.host).ToString();
	std::string port = CefString(&parts.port).ToString();

	if (scheme.empty() || host.empty())
		return {};

	std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	if ((scheme == "http" && port == "80") || (scheme == "https" && port == "443"))
		port.clear();

	return scheme + "://" + host + (port.empty() ? "" : ":" + port);
}

/*static*/
bool BrowserApp::isApprovedTabUrl(const std::string &url)
{
	CefURLParts parts;

	if (!CefParseURL(url, parts))
		return false;

	if (!CefString(&parts.username).empty() || !CefString(&parts.password).empty())
		return false;

	// The parsed origin is lowercased and drops a default port, whatever the url's own spelling
	std::string origin = CefString(&parts.origin).ToString();

	while (!origin.empty() && origin.back() == '/')
		origin.pop_back();

	if (origin.empty())
		return false;

	for (const char *allowed : JavascriptApi::kTabAllowedOrigins)
	{
		if (origin == allowed)
			return true;
	}

	static const std::string testOrigin = []() {
		char buffer[MAX_PATH];
		DWORD len = GetEnvironmentVariableA(JavascriptApi::kTestTabOriginEnvVar, buffer, MAX_PATH);
		std::string value = (len > 0 && len < MAX_PATH) ? std::string(buffer, len) : std::string();

		while (!value.empty() && value.back() == '/')
			value.pop_back();

		return value;
	}();

	return !testOrigin.empty() && origin == testOrigin;
}

void BrowserApp::OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> command_line)
{
	bool enableGPU = command_line->HasSwitch("enable-gpu");
	CefString type = command_line->GetSwitchValue("type");

	if (!enableGPU && type.empty())
	{
		command_line->AppendSwitch("disable-gpu-compositing");
	}

	const char *disableFeaturesDefault = "CalculateNativeWinOcclusion,HardwareMediaKeyHandling,LiveCaption,MediaRouter,StorageNotificationService,WebBluetooth,EnableWindowsGamingInputDataFetcher";

	// Don't override existing, as this can break OSR
	std::string disableFeatures = command_line->GetSwitchValue("disable-features");
	disableFeatures += (disableFeatures.empty() ? "" : ",") + std::string(disableFeaturesDefault);
	command_line->AppendSwitchWithValue("disable-features", disableFeatures);

	std::string disableBlinkFeatures = command_line->GetSwitchValue("disable-blink-features");
	disableBlinkFeatures += (disableBlinkFeatures.empty() ? "" : ",") + std::string("DocumentPictureInPictureAPI");
	command_line->AppendSwitchWithValue("disable-blink-features", disableBlinkFeatures);

	command_line->AppendSwitch("disable-extensions");
	command_line->AppendSwitch("hide-crash-restore-bubble");
	command_line->AppendSwitch("do-not-de-elevate");

	command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");

	// Read from the environment here because this also runs in the helper processes, which inherit it.
	if (int port = getRemoteDebuggingPort())
		command_line->AppendSwitchWithValue("remote-allow-origins", "http://localhost:" + std::to_string(port));
}

BrowserApp::Role BrowserApp::roleOf(CefRefPtr<CefBrowser> browser)
{
	std::lock_guard<std::mutex> grd(m_browserInfoMutex);
	auto itr = m_browserInfos.find(browser->GetIdentifier());
	return itr != m_browserInfos.end() ? itr->second.role : Role::None;
}

bool BrowserApp::OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine>, const CefString &)
{
	// Another instance shares our cache; don't let CEF open a stray default window.
	return true;
}

void BrowserApp::OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> extra_info)
{
	std::lock_guard<std::mutex> grd(m_browserInfoMutex);
	BrowserInfo &info = m_browserInfos[browser->GetIdentifier()];
	++info.instances;

	if (!extra_info)
		return;

	if (extra_info->HasKey("role"))
	{
		const std::string role = extra_info->GetString("role");

		if (role == "main")
			info.role = Role::Main;
		else if (role == "tab")
			info.role = Role::Tab;
	}

	if (extra_info->HasKey("initScript"))
		info.initScript = extra_info->GetString("initScript");
}

void BrowserApp::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser)
{
	const int browserId = browser->GetIdentifier();
	bool lastInstance = false;
	{
		std::lock_guard<std::mutex> grd(m_browserInfoMutex);
		auto itr = m_browserInfos.find(browserId);

		if (itr == m_browserInfos.end() || --itr->second.instances <= 0)
		{
			if (itr != m_browserInfos.end())
				m_browserInfos.erase(itr);
			lastInstance = true;
		}
	}

	// Cross-origin navigation may create the replacement browser before destroying the old one.
	if (lastInstance)
	{
		std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);
		for (auto itr = m_callbackMap.begin(); itr != m_callbackMap.end();)
		{
			if (itr->second.browserId == browserId)
				itr = m_callbackMap.erase(itr);
			else
				++itr;
		}
	}
}

void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
	if (!frame->IsMain())
		return;

	const Role role = roleOf(browser);

	if (role == Role::Main)
	{
		const std::string allowedOrigin = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("api_origin");
		if (allowedOrigin.empty() || OriginOf(frame->GetURL()) != allowedOrigin)
			return;

		CefRefPtr<CefV8Value> globalObj = context->GetGlobal();
		CefRefPtr<CefV8Value> slabsGlobal = CefV8Value::CreateObject(nullptr, nullptr);
		globalObj->SetValue("slabsGlobal", slabsGlobal, V8_PROPERTY_ATTRIBUTE_NONE);
		slabsGlobal->SetValue("pluginVersion", CefV8Value::CreateString(OBS_BROWSER_VERSION_STRING), V8_PROPERTY_ATTRIBUTE_NONE);

		for (auto &itr : JavascriptApi::getPluginFunctionNames())
			slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);

		for (auto &itr : JavascriptApi::getBrowserFunctionNames())
			slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);

		// A page that closes its own window leaves main half closed. Hiding it goes through browser_setHiddenState
		CefRefPtr<CefV8Value> retval;
		CefRefPtr<CefV8Exception> exception;
		context->Eval("window.close = function close() {};", frame->GetURL(), 0, retval, exception);
	}
	else if (role == Role::Tab && isApprovedTabUrl(frame->GetURL()))
	{
		CefRefPtr<CefV8Value> globalObj = context->GetGlobal();
		CefRefPtr<CefV8Value> slabsGlobal = CefV8Value::CreateObject(nullptr, nullptr);
		globalObj->SetValue("slabsTab", slabsGlobal, V8_PROPERTY_ATTRIBUTE_NONE);
		slabsGlobal->SetValue("pluginVersion", CefV8Value::CreateString(OBS_BROWSER_VERSION_STRING), V8_PROPERTY_ATTRIBUTE_NONE);

		for (auto &itr : JavascriptApi::getBrowserTabsFunctionNames())
			slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);

		std::string initScript;

		{
			std::lock_guard<std::mutex> grd(m_browserInfoMutex);
			auto itr = m_browserInfos.find(browser->GetIdentifier());

			if (itr != m_browserInfos.end())
				initScript = itr->second.initScript;
		}

		// Eval, not ExecuteJavaScript: it runs now, before any script of the page
		if (!initScript.empty())
		{
			CefRefPtr<CefV8Value> retval;
			CefRefPtr<CefV8Exception> exception;
			context->Eval(initScript, frame->GetURL(), 0, retval, exception);
		}
	}
}

void BrowserApp::OnContextReleased(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context> context)
{
	std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);
	for (auto itr = m_callbackMap.begin(); itr != m_callbackMap.end();)
	{
		if (itr->second.context->IsSame(context))
			itr = m_callbackMap.erase(itr);
		else
			++itr;
	}
}

bool BrowserApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
	if (message->GetName() == "executeCallback" || message->GetName() == "executeCallback_NoDelete")
	{
		CefRefPtr<CefListValue> arguments = message->GetArgumentList();
		int callbackID = arguments->GetInt(0);
		CefString jsonString = arguments->GetString(1);
		int uuid = arguments->GetInt(2);

		std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);

		auto itr = m_callbackMap.find(callbackID);
		if (itr != m_callbackMap.end() && itr->second.browserId == browser->GetIdentifier())
		{
			CefRefPtr<CefV8Value> function = itr->second.function;
			CefRefPtr<CefV8Context> context = itr->second.context;
			if (!context->IsValid())
			{
				m_callbackMap.erase(itr);
				return true;
			}

			CefV8ValueList args;
			args.push_back(CefV8Value::CreateString(jsonString));

			if (uuid != 0)
				args.push_back(CefV8Value::CreateInt(uuid));

			function->ExecuteFunctionWithContext(context, nullptr, args);

			if (message->GetName() != "executeCallback_NoDelete")
				m_callbackMap.erase(callbackID);
		}
	}
	else if (message->GetName() == "receiverRegistered")
	{
		const int callbackID = message->GetArgumentList()->GetInt(0);
		std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);
		auto current = m_callbackMap.find(callbackID);
		if (current != m_callbackMap.end() && current->second.browserId == browser->GetIdentifier() && current->second.receiverKind != ReceiverKind::None)
		{
			const ReceiverKind kind = current->second.receiverKind;
			for (auto itr = m_callbackMap.begin(); itr != m_callbackMap.end();)
			{
				if (itr->first < callbackID && itr->second.receiverKind == kind && (kind == ReceiverKind::ObsEvents || itr->second.browserId == current->second.browserId))
					itr = m_callbackMap.erase(itr);
				else
					++itr;
			}
		}
	}
	else if (message->GetName() == "executeJavascript")
	{
		CefRefPtr<CefListValue> arguments = message->GetArgumentList();
		browser->GetMainFrame()->ExecuteJavaScript(arguments->GetString(0), browser->GetMainFrame()->GetURL(), 0);
	}

	return true;
}

bool BrowserApp::Execute(const CefString &name, CefRefPtr<CefV8Value>, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &, CefString &)
{
	CefRefPtr<CefV8Context> current = CefV8Context::GetCurrentContext();
	CefRefPtr<CefBrowser> browser = current->GetBrowser();

	// Allowed functions depend on the calling browser, not on shared renderer state
	const std::string funcName = name.ToString();
	const Role role = roleOf(browser);
	const bool allowedForBrowser = role == Role::Main ? (JavascriptApi::isPluginFunctionName(funcName) || JavascriptApi::isBrowserFunctionName(funcName)) : (role == Role::Tab && JavascriptApi::isBrowserTabFunctionName(funcName));
	const std::string allowedOrigin = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("api_origin");
	const bool allowedForOrigin = role == Role::Main ? (!allowedOrigin.empty() && OriginOf(current->GetFrame()->GetURL()) == allowedOrigin) : (role == Role::Tab && isApprovedTabUrl(current->GetFrame()->GetURL()));
	const bool allowed = allowedForBrowser && current->GetFrame()->IsMain() && allowedForOrigin;

	if (allowed)
	{
		int callBackId = 0;
		const int browserId = browser->GetIdentifier();
		const ReceiverKind receiverKind = funcName == "events_registerReceiver" ? ReceiverKind::ObsEvents : (funcName == "tabs_registerMsgReceiver" || funcName == "tab_registerMsgReceiver" ? ReceiverKind::Messages : ReceiverKind::None);
		const bool hasCallback = !arguments.empty() && arguments[0]->IsFunction();

		if (hasCallback)
		{
			std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);
			callBackId = ++m_callbackIdCounter;
			m_callbackMap.emplace(callBackId, CallbackInfo{arguments[0], current, browserId, receiverKind});
		}

		CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create(name);
		CefRefPtr<CefListValue> args = msg->GetArgumentList();
		args->SetInt(0, callBackId);

		/* Pass on arguments */
		for (u_long l = 0; l < arguments.size(); l++)
		{
			u_long pos;
			if (arguments[0]->IsFunction())
				pos = l;
			else
				pos = l + 1;

			if (arguments[l]->IsString())
				args->SetString(pos, arguments[l]->GetStringValue());
			else if (arguments[l]->IsInt())
				args->SetInt(pos, arguments[l]->GetIntValue());
			else if (arguments[l]->IsBool())
				args->SetBool(pos, arguments[l]->GetBoolValue());
			else if (arguments[l]->IsDouble())
				args->SetDouble(pos, arguments[l]->GetDoubleValue());
		}

		SendBrowserProcessMessage(browser, PID_BROWSER, msg);
	}
	else
	{
		/* Function does not exist. */
		return false;
	}

	return true;
}
