#include "GrpcPlugin.h"
#include "JavascriptApi.h"
#include "PluginJsHandler.h"

#include <filesystem>

/***
* Server
* Receiving messages from the browser
*/

class grpc_plugin_objImpl final : public grpc_plugin_obj::Service
{
	grpc::Status com_grpc_js_api(grpc::ServerContext *context, const grpc_js_api_Request *request, grpc_js_api_Reply *response) override
	{
		PluginJsHandler::instance().pushApiRequest(request->funcname(), request->params());
		return grpc::Status::OK;
	}
};

/***
* Client
* Sending messages to the browser
*/

grpc_plugin_objClient::grpc_plugin_objClient(std::shared_ptr<grpc::Channel> channel) : stub_(grpc_proxy_obj::NewStub(channel))
{
	m_connected = channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(3));
}

bool grpc_plugin_objClient::send_executeCallback(const int functionId, const std::string &jsonStr)
{
	grpc_js_api_ExecuteCallback request;
	request.set_funcid(functionId);
	request.set_jsonstr(jsonStr.c_str());

	grpc_js_api_Reply reply;
	grpc::ClientContext context;
	grpc::Status status = stub_->com_grpc_js_executeCallback(&context, request, &reply);

	if (!status.ok())
		return m_connected = false;

	return true;
}

bool grpc_plugin_objClient::send_executeJavascript(const std::string& codeStr)
{
	grpc_run_javascriptOnBrowser request;
	request.set_str(codeStr.c_str());

	grpc_empty_Reply reply;
	grpc::ClientContext context;
	grpc::Status status = stub_->com_grpc_run_javascriptOnBrowser(&context, request, &reply);

	if (!status.ok())
		return m_connected = false;

	return true;
}

bool grpc_plugin_objClient::send_windowToggleVisibility()
{
	grpc_window_toggleVisibility request;

	grpc_empty_Reply reply;
	grpc::ClientContext context;
	grpc::Status status = stub_->com_grpc_window_toggleVisibility(&context, request, &reply);

	if (!status.ok())
		return m_connected = false;

	return true;
}

// Grpc
//

GrpcPlugin::GrpcPlugin() {}

GrpcPlugin::~GrpcPlugin() {}

bool GrpcPlugin::startServer(int32_t listenPort)
{
	// Don't repeat
	if (m_listenPort != 0)
		return false;

	m_listenPort = listenPort;

	grpc::EnableDefaultHealthCheckService(true);
	grpc::reflection::InitProtoReflectionServerBuilderPlugin();

	m_builder = std::make_unique<grpc::ServerBuilder>();
	m_builder->AddListeningPort(std::string("localhost:") + std::to_string(m_listenPort), grpc::InsecureServerCredentials());

	m_serverObj = std::make_unique<grpc_plugin_objImpl>();
	m_builder->RegisterService(m_serverObj.get());

	m_server = m_builder->BuildAndStart();

	return m_server != nullptr;
}

bool GrpcPlugin::connectToClient(int32_t portNumber)
{
	m_clientObj = std::make_unique<grpc_plugin_objClient>(grpc::CreateChannel("localhost:" + std::to_string(portNumber), grpc::InsecureChannelCredentials()));
	return m_clientObj != nullptr;
}

void GrpcPlugin::stop()
{
	if (m_server != nullptr)
	{
		m_server->Shutdown();
		m_server->Wait();
		m_server.reset();
	}

	m_clientObj = nullptr;
	m_serverObj = nullptr;
	m_builder = nullptr;
}
