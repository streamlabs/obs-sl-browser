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

bool BrowserApp::OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine>, const CefString &)
{
	// Another instance shares our cache; don't let CEF open a stray default window.
	return true;
}

void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
	std::string allowedOrigin = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("api_origin");

	if (!frame->IsMain() || allowedOrigin.empty() || OriginOf(frame->GetURL()) != allowedOrigin)
		return;

	CefRefPtr<CefV8Value> globalObj = context->GetGlobal();

	CefRefPtr<CefV8Value> slabsGlobal = CefV8Value::CreateObject(nullptr, nullptr);
	globalObj->SetValue("slabsGlobal", slabsGlobal, V8_PROPERTY_ATTRIBUTE_NONE);
	slabsGlobal->SetValue("pluginVersion", CefV8Value::CreateString(OBS_BROWSER_VERSION_STRING), V8_PROPERTY_ATTRIBUTE_NONE);

	for (auto &itr : JavascriptApi::getPluginFunctionNames())
		slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);

	for (auto &itr : JavascriptApi::getBrowserFunctionNames())
		slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);	
}

bool BrowserApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
	if (message->GetName() == "executeCallback")
	{
		CefRefPtr<CefListValue> arguments = message->GetArgumentList();
		int callbackID = arguments->GetInt(0);
		CefString jsonString = arguments->GetString(1);

		std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);

		if (CefRefPtr<CefV8Value> function = m_callbackMap[callbackID].first)
		{
			CefRefPtr<CefV8Context> context = m_callbackMap[callbackID].second;

			CefV8ValueList args;
			args.push_back(CefV8Value::CreateString(jsonString));
			function->ExecuteFunctionWithContext(context, nullptr, args);

			m_callbackMap.erase(callbackID);
		}
	}

	if (message->GetName() == "executeJavascript")
	{
		CefRefPtr<CefListValue> arguments = message->GetArgumentList();
		browser->GetMainFrame()->ExecuteJavaScript(arguments->GetString(0), browser->GetMainFrame()->GetURL(), 0); 
	}

	return true;
}

bool BrowserApp::Execute(const CefString &name, CefRefPtr<CefV8Value>, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &, CefString &)
{
	if (JavascriptApi::isValidFunctionName(name.ToString()))
	{
		int callBackId = 0;

		if (arguments.size() >= 1 && arguments[0]->IsFunction())
		{
			std::lock_guard<std::recursive_mutex> grd(m_callbackMutex);
			callBackId = ++m_callbackIdCounter;
			m_callbackMap[callBackId] = {arguments[0], CefV8Context::GetCurrentContext()};
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

		CefRefPtr<CefBrowser> browser = CefV8Context::GetCurrentContext()->GetBrowser();
		SendBrowserProcessMessage(browser, PID_BROWSER, msg);
	}
	else
	{
		/* Function does not exist. */
		return false;
	}

	return true;
}
