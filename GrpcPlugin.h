#pragma once

#include "sl_browser_api.grpc.pb.h"

#include <filesystem>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

class grpc_plugin_objClient
{
public:
	grpc_plugin_objClient(std::shared_ptr<grpc::Channel> channel);

	bool send_executeCallback(const int functionId, const std::string &jsonStr);
	bool send_windowToggleVisibility();

private:
	std::atomic<bool> m_connected{false};
	std::unique_ptr<grpc_proxy_obj::Stub> stub_Browser;
	std::unique_ptr<grpc_browser_window_obj::Stub> stub_BrowserWindow;
};

class GrpcPlugin
{
public:
	static GrpcPlugin &instance()
	{
		static GrpcPlugin a;
		return a;
	}

	bool connectToClient(int32_t port);
	bool startServer(int32_t port);

	void stop();

	auto getClient() const { return m_clientObj.get(); }

private:
	GrpcPlugin();
	~GrpcPlugin();

	int32_t m_listenPort{0};

	std::wstring m_modulePath;
	std::unique_ptr<grpc::Server> m_server;
	std::unique_ptr<grpc::ServerBuilder> m_builder;
	std::unique_ptr<grpc_plugin_obj::Service> m_serverObj;
	std::unique_ptr<grpc_plugin_objClient> m_clientObj;
};
