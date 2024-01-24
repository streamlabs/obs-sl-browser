#pragma once

#include "sl_browser_api.grpc.pb.h"

#include <filesystem>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

class grpc_proxy_objClient
{
public:
	grpc_proxy_objClient(std::shared_ptr<grpc::Channel> channel);

	bool send_js_api(const std::string &funcName, const std::string &params);

	std::atomic<bool> m_connected{false};

private:
	std::unique_ptr<grpc_plugin_obj::Stub> stub_;
};

class GrpcBrowser
{
public:
	static GrpcBrowser &instance()
	{
		static GrpcBrowser a;
		return a;
	}

	bool connectToClient(int32_t port);
	bool startServer(int32_t port);

	void stop();

	auto *getClient() { return m_clientObj.get(); }

private:
	GrpcBrowser();
	~GrpcBrowser();

	int32_t m_listenPort{0};

	std::wstring m_modulePath;
	std::unique_ptr<grpc::Server> m_server;
	std::unique_ptr<grpc::ServerBuilder> m_builder;
	std::unique_ptr<grpc_proxy_obj::Service> m_serverObj;
	std::unique_ptr<grpc_proxy_objClient> m_clientObj;
};
