#include "browser-app.hpp"
#include "browser-version.h"
#include "JavascriptApi.h"

#include <json11/json11.hpp>

#include <windows.h>

using namespace json11;

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
}

void BrowserApp::OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> command_line)
{
	bool enableGPU = command_line->HasSwitch("enable-gpu");
	CefString type = command_line->GetSwitchValue("type");

	if (!enableGPU && type.empty())
	{
		command_line->AppendSwitch("disable-gpu-compositing");
	}

	if (command_line->HasSwitch("disable-features"))
	{
		// Don't override existing, as this can break OSR
		std::string disableFeatures = command_line->GetSwitchValue("disable-features");
		disableFeatures += ",HardwareMediaKeyHandling";
		command_line->AppendSwitchWithValue("disable-features", disableFeatures);
	}
	else
	{
		command_line->AppendSwitchWithValue("disable-features", "HardwareMediaKeyHandling");
	}

	command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
	command_line->AppendSwitchWithValue("remote-allow-origins", "http://localhost:9123");
}

void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context> context)
{
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
