#pragma once

#include "sl_browser_api.grpc.pb.h"

#include <filesystem>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

class grpc_browser_window_objClient
{
public:
	grpc_browser_window_objClient(std::shared_ptr<grpc::Channel> channel);
	std::atomic<bool> m_connected{false};

private:
	std::unique_ptr<grpc_plugin_obj::Stub> stub_;
};

class GrpcBrowserWindow
{
public:
	static GrpcBrowserWindow &instance()
	{
		static GrpcBrowserWindow a;
		return a;
	}

	bool connectToClient(int32_t port);
	bool startServer(int32_t port);

	void stop();

	auto *getClient() { return m_clientObj.get(); }

private:
	GrpcBrowserWindow();
	~GrpcBrowserWindow();

	int32_t m_listenPort{0};

	std::wstring m_modulePath;
	std::unique_ptr<grpc::Server> m_server;
	std::unique_ptr<grpc::ServerBuilder> m_builder;
	std::unique_ptr<grpc_browser_window_obj::Service> m_serverObj;
	std::unique_ptr<grpc_browser_window_objClient> m_clientObj;
};
