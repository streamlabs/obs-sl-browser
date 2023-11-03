#include "SlBrowser.h"
#include "SlBrowserWidget.h"
#include "GrpcBrowser.h"
#include "CrashHandler.h"

#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include <filesystem>

#include <QTimer>
#include <QPointer>
#include <QVBoxLayout>
#include <QDialog>
#include <QMainWindow>
#include <QAbstractButton>
#include <QPushButton>
#include <QApplication>

#include "browser-scheme.hpp"
#include "browser-version.h"
#include "json11/json11.hpp"
#include "cef-headers.hpp"
#include "ConsoleToggle.h"

#include <include/base/cef_callback.h>
#include <include/wrapper/cef_closure_task.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <ShlObj.h>
#include <iostream>

using namespace std;
using namespace json11;

SlBrowser::SlBrowser() {}

SlBrowser::~SlBrowser() {}

void SlBrowser::run(int argc, char *argv[])
{
	SpawnConsoleToggle();

	if (argc < 4)
	{
		// todo: logging
		printf("Not enough args.\n");
		return;
	}

	m_obs64_PIDt = atoi(argv[1]);
	int32_t parentListenPort = atoi(argv[2]);
	int32_t myListenPort = atoi(argv[3]);

	if (!GrpcBrowser::instance().startServer(myListenPort))
	{
		printf("sl-proxy: failed to start grpc server, GetLastError = %d\n", GetLastError());
		return;
	}

	if (!GrpcBrowser::instance().connectToClient(parentListenPort))
	{
		printf("sl-proxy: failed to connected to plugin's grpc server, GetLastError = %d\n", GetLastError());
		return;
	}

	QApplication a(argc, argv);

	// Create CEF Browser
	auto manager_thread = thread(&SlBrowser::browserManagerThread, this);

	// Create Qt Widget
	m_widget = new SlBrowserWidget{};
	m_widget->setWindowTitle("Streamlabs");
	m_widget->setMinimumSize(320, 240);
	m_widget->resize(1280, 720);
	m_widget->show();

	CefPostTask(TID_UI, base::BindOnce(&CreateCefBrowser, 5));

	std::thread(CheckForObsThread).detach();

	// todo: remove this
	std::thread(DebugInputThread).detach();

	// Run Qt Application
	int result = a.exec();
}

void SlBrowser::CreateCefBrowser(int arg)
{
	auto &app = SlBrowser::instance();

	CefWindowInfo window_info;
	CefBrowserSettings browser_settings;
	app.browserClient = new BrowserClient(false);

	// TODO
	CefString url = "https://obs-plugin.streamlabs.dev";

	// Now set the parent of the CEF browser to the QWidget
	window_info.SetAsChild((HWND)app.m_widget->winId(), CefRect(0, 0, app.m_widget->width(), app.m_widget->height()));
	app.m_browser = CefBrowserHost::CreateBrowserSync(window_info, app.browserClient.get(), url, browser_settings, CefRefPtr<CefDictionaryValue>(), nullptr);

	auto bringToTop = [] {
		// For the next second keep the window on top
		for (int i = 0; i < 10; ++i)
		{
			::SetForegroundWindow((HWND)SlBrowser::instance().m_widget->winId());
			::Sleep(100);
		}
	};

	std::thread(bringToTop).detach();
}

void SlBrowser::browserInit()
{
	TCHAR moduleFileName[MAX_PATH]{};
	GetModuleFileName(NULL, moduleFileName, MAX_PATH);
	std::filesystem::path fsPath(moduleFileName);

	std::string path = fsPath.remove_filename().string();
	path += "sl-browser-page.exe";

	CefMainArgs args;

	CefSettings settings;
	settings.log_severity = LOGSEVERITY_VERBOSE;

	CefString(&settings.user_agent_product) = "Streamlabs";

	// TODO
	CefString(&settings.locale) = "en-US";
	CefString(&settings.accept_language_list) = "en-US,en";

	settings.persist_user_preferences = 1;

	char cache_path[MAX_PATH];
	std::string cache_pathStdStr;

	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, cache_path)))
	{
		cache_pathStdStr = std::string(cache_path) + "\\StreamlabsOBS_CEF_Cache";
		CefString(&settings.cache_path) = cache_pathStdStr;
	}

	CefString(&settings.browser_subprocess_path) = path.c_str();

	if (!cache_pathStdStr.empty())
	{
		std::string logPathFile = cache_pathStdStr + "\\cef.log";
		CefString(&settings.log_file) = logPathFile;
		settings.log_severity = LOGSEVERITY_DEBUG;
		CrashHandler::instance().setLogfilePath(logPathFile);
	}

	// Set the remote debugging port
	settings.remote_debugging_port = 9123;

	m_app = new BrowserApp();

	CefExecuteProcess(args, m_app, nullptr);
	CefInitialize(args, settings, m_app, nullptr);

	// Register http://absolute/ scheme handler for older CEF builds which do not support file:// URLs
	CefRegisterSchemeHandlerFactory("http", "absolute", new BrowserSchemeHandlerFactory());
}

void SlBrowser::browserShutdown()
{
	CefClearSchemeHandlerFactories();
	CefShutdown();
	m_app = nullptr;
}

void SlBrowser::browserManagerThread()
{
	browserInit();
	CefRunMessageLoop();
	browserShutdown();
}

/*static*/
void SlBrowser::CheckForObsThread()
{
	while (true)
	{
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, SlBrowser::instance().m_obs64_PIDt);
		if (hProcess == NULL)
		{
			// If OpenProcess fails, it might mean the process does not exist
			DWORD error = GetLastError();
			if (error == ERROR_INVALID_PARAMETER)
			{
				abort();
			}
		}
		else
		{
			DWORD exitCode;
			if (GetExitCodeProcess(hProcess, &exitCode))
			{
				if (exitCode != STILL_ACTIVE)
				{
					// The process exists but is no longer active
					CloseHandle(hProcess);
					abort();
				}
			}
			CloseHandle(hProcess);
		}

		// Sleep for 10ms before checking again
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

// todo: remove this

/*static*/
void SlBrowser::DebugInputThread()
{
	::Sleep(2000);

	printf("\n\n>>>>BROWSER CONSOLE:\n\n");

	std::string url;

	while (true)
	{
		if (GetConsoleWindow() == NULL)
		{
			::Sleep(1);
			continue;
		}

		std::cout << "Enter URL: ";
		std::cin >> url;

		if (!url.empty())
		{
			std::cout << ";" << std::endl;
			SlBrowser::instance().m_browser->GetMainFrame()->LoadURL(url);
		}
		else
		{
			std::cout << "URL cannot be empty. Please try again." << std::endl;
		}

		::Sleep(1000);
	}
}
