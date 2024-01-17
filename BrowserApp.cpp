#include "BrowserApp.h"
#include "browser-version.h"
#include "JavascriptApi.h"
#include "GrpcBrowser.h"

#include <json11/json11.hpp>
#include "include/base/cef_logging.h"

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
}

void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefRefPtr<CefV8Context> context)
{
	CefRefPtr<CefV8Value> slabsGlobal = CefV8Value::CreateObject(nullptr, nullptr);
	context->GetGlobal()->SetValue("slabsGlobal", slabsGlobal, V8_PROPERTY_ATTRIBUTE_NONE);

	for (auto &itr : JavascriptApi::getPluginFunctionNames())
		slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);

	for (auto &itr : JavascriptApi::getBrowserFunctionNames())
		slabsGlobal->SetValue(itr.first, CefV8Value::CreateFunction(itr.first, this), V8_PROPERTY_ATTRIBUTE_NONE);

	// Notify central of our existence so that they can tell us the port that the plugin is listening on
	SendBrowserProcessMessage(CefV8Context::GetCurrentContext()->GetBrowser(), PID_BROWSER, CefProcessMessage::Create("OnContextCreated"));
}

bool BrowserApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
	std::lock_guard<std::recursive_mutex> grd(m_executeMtx);

	if (message->GetName() == "GrpcPort")
	{
		// Start listening
		if (GrpcBrowser::instance().startServer(0))
		{
			CefRefPtr<CefListValue> arguments = message->GetArgumentList();

			if (!GrpcBrowser::instance().connectToClient(arguments->GetInt(0)))
			{
				LOG(ERROR) << "Context process unable to connect to plugin's Grpc!";
			}
			else
			{
				m_connected = true;

				CefString nil;
				CefRefPtr<CefV8Value> nil2;

				// Handle any Javascript calls that we weren't able to yet because we were busy connecting to the plugin
				for (auto &itr : m_backlog)
					Execute(itr.first, nullptr, itr.second, nil2, nil);

				m_backlog.clear();
			}
		}
		else
		{
			LOG(ERROR) << "Context process unable to start their Grpc server!";
		}
	}

	if (message->GetName() == "BrowserWindowCallback")
	{
		CefRefPtr<CefListValue> arguments = message->GetArgumentList();
		int callbackID = arguments->GetInt(0);
		CefString jsonString = arguments->GetString(1);

		if (CefRefPtr<CefV8Value> function = m_callbackMap[callbackID].first)
		{
			CefRefPtr<CefV8Context> context = m_callbackMap[callbackID].second;

			CefV8ValueList args;
			args.push_back(CefV8Value::CreateString(jsonString));
			function->ExecuteFunctionWithContext(context, nullptr, args);

			m_callbackMap.erase(callbackID);
		}
	}

	return true;
}

bool BrowserApp::Execute(const CefString &name, CefRefPtr<CefV8Value> /*unused, dont without revising OnProcessMessageReceived*/, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> & /*unused*/, CefString & /*unused*/)
{
	std::lock_guard<std::recursive_mutex> grd(m_executeMtx);

	if (JavascriptApi::isValidFunctionName(name.ToString()))
	{
		if (!m_connected)
		{
			m_backlog.push_back({ name, arguments });
		}
		else
		{
			int callBackId = 0;

			if (arguments.size() >= 1 && arguments[0]->IsFunction())
			{
				callBackId = ++m_callbackIdCounter;
				m_callbackMap[callBackId] = { arguments[0], CefV8Context::GetCurrentContext() };
			}

			CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create(name);
			CefRefPtr<CefListValue> args = msg->GetArgumentList();
			args->SetInt(0, callBackId);

			// Pass on arguments
			for (size_t l = 0; l < arguments.size(); l++)
			{
				size_t pos;
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

			if (JavascriptApi::isBrowserFunctionName(name))
			{
				// Send to the window, they'll phone back when they have an answer
				SendBrowserProcessMessage(CefV8Context::GetCurrentContext()->GetBrowser(), PID_BROWSER, msg);
			}
			else
			{
				// Send to the plugin, they'll phone back when they have an answer
				grpc_js_api_Reply reply;
				GrpcBrowser::instance().getClient()->send_js_api(name, cefListValueToJSONString(args));
			}
		}
	}
	else
	{
		/* Function does not exist. */
		return false;
	}

	return true;
}

bool BrowserApp::PopCallback(const int32_t funcId, std::pair<CefRefPtr<CefV8Value>, CefRefPtr<CefV8Context>>& output)
{
	std::lock_guard<std::recursive_mutex> grd(m_executeMtx);

	auto itr = m_callbackMap.find(funcId);

	if (itr != m_callbackMap.end())
	{
		itr->second.swap(output);
		m_callbackMap.erase(itr);
		return true;
	}

	return false;
}

/*static*/
json11::Json convertCefValueToJSON(CefRefPtr<CefValue> value)
{
	switch (value->GetType())
	{
	case VTYPE_NULL:
		return nullptr;

	case VTYPE_BOOL:
		return value->GetBool();

	case VTYPE_INT:
		return value->GetInt();

	case VTYPE_DOUBLE:
		return value->GetDouble();

	case VTYPE_STRING:
		return value->GetString().ToString();

	case VTYPE_LIST: {
		const auto &list = value->GetList();
		std::vector<json11::Json> jsonList;
		for (size_t i = 0; i < list->GetSize(); ++i)
		{
			jsonList.push_back(convertCefValueToJSON(list->GetValue(i)));
		}
		return jsonList;
	}

	case VTYPE_DICTIONARY: {
		const auto &dict = value->GetDictionary();
		std::map<std::string, json11::Json> jsonMap;
		CefDictionaryValue::KeyList keys;
		dict->GetKeys(keys);
		for (const auto &key : keys)
		{
			jsonMap[key] = convertCefValueToJSON(dict->GetValue(key));
		}
		return jsonMap;
	}

	default:
		return nullptr;
	}
}

/*static*/
std::string BrowserApp::cefListValueToJSONString(CefRefPtr<CefListValue> listValue)
{
	std::map<std::string, json11::Json> jsonMap;
	for (size_t i = 0; i < listValue->GetSize(); ++i)
	{
		// Convert index to string key like "param1", "param2", ...
		std::string key = "param" + std::to_string(i + 1);
		jsonMap[key] = convertCefValueToJSON(listValue->GetValue(i));
	}

	std::string json_str;
	json_str = json11::Json(jsonMap).dump();
	return json_str;
}
