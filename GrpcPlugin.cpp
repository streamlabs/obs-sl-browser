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
		std::string jsonReply;
		PluginJsHandler::instance().handleApiRequest(request->funcname(), request->params(), request->peerport(), &jsonReply);

		if (!jsonReply.empty())
			response->set_jsonresponse(jsonReply);

		return grpc::Status::OK;
	}

	grpc::Status com_grpc_hello(grpc::ServerContext *context, const grpc_hello_Request *request, grpc_js_api_Reply *response) override
	{
		if (GrpcPlugin::instance().getClientToBrowserSubProcess(request->peerport()) == nullptr)
		{
			if (!GrpcPlugin::instance().connectToBrowserSubProcess(request->peerport()))
				blog(LOG_ERROR, "com_grpc_js_api failed to connectToBrowserSubProcess");
		}

		return grpc::Status::OK;
	}	
};

/***
* Client 1
* Sending messages to the browser's sub processes
*/

grpc_plugin_to_proxy_objClient::grpc_plugin_to_proxy_objClient(std::shared_ptr<grpc::Channel> channel) :
	stub_(grpc_proxy_obj::NewStub(channel))
{
	m_connected = channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(3));
}

bool grpc_plugin_to_proxy_objClient::send_executeCallback(const int functionId, const std::string &jsonStr)
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

/***
* Client 2
* Sending messages to the browser's main window process
*/

grpc_plugin_to_window_objClient::grpc_plugin_to_window_objClient(std::shared_ptr<grpc::Channel> channel) :
	stub_(grpc_browser_window_obj::NewStub(channel))
{
	m_connected = channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(3));
}

bool grpc_plugin_to_window_objClient::send_windowToggleVisibility()
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

bool GrpcPlugin::connectToBrowserWindow(int32_t portNumber)
{
	m_clientConnToBrowserWindow = std::make_unique<grpc_plugin_to_window_objClient>(grpc::CreateChannel("localhost:" + std::to_string(portNumber), grpc::InsecureChannelCredentials()));
	return true;
}

bool GrpcPlugin::connectToBrowserSubProcess(int32_t portNumber)
{
	std::lock_guard<std::recursive_mutex> grd(m_mutex);
	m_clientConnToBrowserSubProcess[portNumber] = std::make_unique<grpc_plugin_to_proxy_objClient>(grpc::CreateChannel("localhost:" + std::to_string(portNumber), grpc::InsecureChannelCredentials()));
	return true;
}

grpc_plugin_to_proxy_objClient* GrpcPlugin::getClientToBrowserSubProcess(const int32_t port)
{
	std::lock_guard<std::recursive_mutex> grd(m_mutex);
	auto itr = m_clientConnToBrowserSubProcess.find(port);

	if (itr != m_clientConnToBrowserSubProcess.end())
		return itr->second.get();

	blog(LOG_ERROR, "GrpcPlugin failed to find connection to peer port %d", port);
	return nullptr;
}

void GrpcPlugin::stop()
{
	if (m_server != nullptr)
	{
		m_server->Shutdown();
		m_server->Wait();
		m_server.reset();
	}

	m_clientConnToBrowserWindow = nullptr;
	m_clientConnToBrowserSubProcess.clear();
	m_serverObj = nullptr;
	m_builder = nullptr;
}
