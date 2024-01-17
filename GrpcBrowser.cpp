#include "GrpcBrowser.h"
#include "WindowsFunctions.h"
#include "BrowserApp.h"

#include "cef-headers.hpp"

#include <filesystem>

/***
* Server
* Receiving messages from the plugin
*/

class grpc_proxy_objImpl final : public grpc_proxy_obj::Service
{
	grpc::Status com_grpc_js_executeCallback(grpc::ServerContext *context, const grpc_js_api_ExecuteCallback *request, grpc_js_api_Reply *response) override
	{
		CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("executeCallback");
		CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();

		std::pair<CefRefPtr<CefV8Value>, CefRefPtr<CefV8Context>> callbackInfo;

		if (BrowserApp::instance().PopCallback(request->funcid(), callbackInfo))
		{
			if (callbackInfo.first != nullptr)
			{
				CefRefPtr<CefV8Value> function = callbackInfo.first;
				CefRefPtr<CefV8Context> context = callbackInfo.second;

				CefV8ValueList args;
				args.push_back(CefV8Value::CreateString(request->jsonstr()));
				callbackInfo.first->ExecuteFunctionWithContext(context, nullptr, args);
			}
		}

		return grpc::Status::OK;
	}
};

/***
* Client
* Sending messages to the plugin
*/

grpc_proxy_objClient::grpc_proxy_objClient(std::shared_ptr<grpc::Channel> channel) : stub_(grpc_plugin_obj::NewStub(channel))
{
	m_connected = channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(3));
}

bool grpc_proxy_objClient::send_js_api(const std::string &funcName, const std::string &params)
{
	grpc_js_api_Request request;
	request.set_funcname(funcName);
	request.set_params(params);

	grpc_js_api_Reply reply;
	grpc::ClientContext context;
	grpc::Status status = stub_->com_grpc_js_api(&context, request, &reply);

	if (!status.ok())
		return m_connected = false;

	return true;
}

// Grpc
//

GrpcBrowser::GrpcBrowser() {}

GrpcBrowser::~GrpcBrowser() {}

bool GrpcBrowser::startServer(int32_t listenPort)
{
	// Don't repeat
	if (m_listenPort != 0)
		return false;

	int32_t selectedPort = 0;
	std::string server_address = "localhost:" + std::to_string(listenPort);

	grpc::EnableDefaultHealthCheckService(true);
	grpc::reflection::InitProtoReflectionServerBuilderPlugin();

	m_builder = std::make_unique<grpc::ServerBuilder>();
	m_builder->AddListeningPort(server_address, grpc::InsecureServerCredentials(), &selectedPort);

	m_listenPort = selectedPort;

	m_serverObj = std::make_unique<grpc_proxy_objImpl>();
	m_builder->RegisterService(m_serverObj.get());

	m_server = m_builder->BuildAndStart();
	return m_server != nullptr;
}

bool GrpcBrowser::connectToClient(int32_t portNumber)
{
	m_clientObj = std::make_unique<grpc_proxy_objClient>(grpc::CreateChannel("localhost:" + std::to_string(portNumber), grpc::InsecureChannelCredentials()));

	return m_clientObj != nullptr;
}

void GrpcBrowser::stop() {}
