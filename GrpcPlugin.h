#pragma once

#include "sl_browser_api.grpc.pb.h"

#include <filesystem>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

class grpc_plugin_to_proxy_objClient
{
public:
	grpc_plugin_to_proxy_objClient(std::shared_ptr<grpc::Channel> channel);
	bool send_executeCallback(const int functionId, const std::string &jsonStr);

private:
	std::atomic<bool> m_connected{false};
	std::unique_ptr<grpc_proxy_obj::Stub> stub_;
};

class grpc_plugin_to_window_objClient
{
public:
	grpc_plugin_to_window_objClient(std::shared_ptr<grpc::Channel> channel);
	bool send_windowToggleVisibility();

private:
	std::atomic<bool> m_connected{false};
	std::unique_ptr<grpc_browser_window_obj::Stub> stub_;
};

class GrpcPlugin
{
public:
	static GrpcPlugin &instance()
	{
		static GrpcPlugin a;
		return a;
	}

	bool connectToBrowserWindow(int32_t port);
	bool connectToBrowserSubProcess(int32_t port);
	bool startServer(int32_t port);

	void stop();

	auto getClientToBrowserWindow() const { return m_clientConnToBrowserWindow.get(); }
	grpc_plugin_to_proxy_objClient *getClientToBrowserSubProcess(const int32_t port);

private:
	GrpcPlugin();
	~GrpcPlugin();

	int32_t m_listenPort{0};

	std::wstring m_modulePath;
	std::unique_ptr<grpc::Server> m_server;
	std::unique_ptr<grpc::ServerBuilder> m_builder;
	std::unique_ptr<grpc_plugin_obj::Service> m_serverObj;
	std::unique_ptr<grpc_plugin_to_window_objClient> m_clientConnToBrowserWindow;
	std::map<int32_t, std::unique_ptr<grpc_plugin_to_proxy_objClient>> m_clientConnToBrowserSubProcess;

	std::recursive_mutex m_mutex;
};
