#include "GrpcBrowserWindow.h"
#include "SlBrowser.h"
#include "WindowsFunctions.h"

#include <filesystem>

/***
* Server
* Receiving messages from the plugin
*/

class grpc_browser_window_objImpl final : public grpc_browser_window_obj::Service
{
	grpc::Status com_grpc_window_toggleVisibility(grpc::ServerContext *context, const grpc_window_toggleVisibility *request, grpc_empty_Reply *response) override
	{		  	
		// If hidden
		if (SlBrowser::instance().m_widget->isHidden())
		{
			// Main page isn't loading and it also failed
			if (!SlBrowser::instance().getMainLoadingInProgress() && !SlBrowser::instance().getMainPageSuccess())
			{
				// Attempt load default URL again
				SlBrowser::instance().setMainLoadingInProgress(true);
				SlBrowser::instance().m_browser->GetMainFrame()->LoadURL(SlBrowser::getDefaultUrl());
			}
		}

		SlBrowser::instance().m_widget->setHidden(!SlBrowser::instance().m_widget->isHidden());

		if (!SlBrowser::instance().m_widget->isHidden())
		{
			HWND hwnd = HWND(SlBrowser::instance().m_widget->winId());
			WindowsFunctions::ForceForegroundWindow(hwnd);
		}

		SlBrowser::instance().saveHiddenState(SlBrowser::instance().m_widget->isHidden());
		return grpc::Status::OK;
	}
};

/***
* Client
* Sending messages to the plugin
*/

grpc_browser_window_objClient::grpc_browser_window_objClient(std::shared_ptr<grpc::Channel> channel) : stub_(grpc_plugin_obj::NewStub(channel))
{
	m_connected = channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(3));
}

// Grpc
//

GrpcBrowserWindow::GrpcBrowserWindow() {}

GrpcBrowserWindow::~GrpcBrowserWindow() {}

bool GrpcBrowserWindow::startServer(int32_t listenPort)
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

	m_serverObj = std::make_unique<grpc_browser_window_objImpl>();
	m_builder->RegisterService(m_serverObj.get());

	m_server = m_builder->BuildAndStart();
	return m_server != nullptr;
}

bool GrpcBrowserWindow::connectToClient(int32_t portNumber)
{
	m_clientObj = std::make_unique<grpc_browser_window_objClient>(grpc::CreateChannel("localhost:" + std::to_string(portNumber), grpc::InsecureChannelCredentials()));

	return m_clientObj != nullptr;
}

void GrpcBrowserWindow::stop() {}
