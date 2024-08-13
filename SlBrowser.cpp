#include "SlBrowser.h"
#include "SlBrowserWidget.h"
#include "GrpcBrowser.h"
#include "CrashHandler.h"

#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>

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

BrowserElements::~BrowserElements()
{
	CefPostTask(TID_UI, base::BindOnce(&queueCleanupQtObj, widget));
}

void SlBrowser::run(int argc, char *argv[])
{
	QCoreApplication::addLibraryPath("../../bin/64bit/");

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

	// Background threads
	std::thread(CheckForObsThread).detach();
	std::thread(DebugInputThread).detach();
	
	m_qapp = std::make_unique<QApplication>(argc, argv);

	// Create CEF Browser
	auto manager_thread = thread(&SlBrowser::browserManagerThread, this);

	while (!m_cefInit)
		::Sleep(1);

	// Main browser
	//

	m_mainBrowser = std::make_shared<BrowserElements>();
	m_mainBrowser->widget = new SlBrowserWidget;
	m_mainBrowser->widget->setWindowTitle("Streamlabs");
	m_mainBrowser->widget->setMinimumSize(320, 240);
	m_mainBrowser->widget->resize(1280, 720);

	// We have to show before creating CEF because it needs the HWND, and the HWND is not made until the QtWidget is shown at least once
	m_mainBrowser->widget->showMinimized();

	createCefBrowser(0, m_mainBrowser, URL::getPluginHttpUrl(), SlBrowser::instance().getSavedHiddenState(), true);

	// Appstore
	//

	{
		//auto appstoreBrowser = std::make_shared<BrowserElements>();
		//appstoreBrowser->widget = new SlBrowserWidget;
		//appstoreBrowser->widget->setWindowTitle("Streamlabs App Store");
		//appstoreBrowser->widget->setMinimumSize(320, 240);
		//appstoreBrowser->widget->resize(1280, 720);
		//appstoreBrowser->widget->showMinimized();
		//
		//createCefBrowser(1, appstoreBrowser, "https://streamlabs.com/sl-desktop-app-store", false, false);
	}

	// Run Qt Application
	int result = m_qapp->exec();
}

void SlBrowser::createCefBrowser(const int32_t uuid, std::shared_ptr<BrowserElements> browserElements, const std::string& url, const bool startHidden, const bool keepOnTop)
{
	std::lock_guard<std::mutex> g(m_mutex);

	m_lastError.clear();

	if (m_browsers.find(uuid) != m_browsers.end())
	{
		m_lastError = "createCefBrowser, uuid already exists";
		return;
	}

	m_browsers[uuid] = browserElements;

	CefPostTask(TID_UI, base::BindOnce(&createCefBrowser_internal, browserElements, url, startHidden, keepOnTop));
}

/*static*/
void SlBrowser::createCefBrowser_internal(std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop)
{
	CefWindowInfo window_info;
	CefBrowserSettings browser_settings;
	browserElements->client = new BrowserClient(false);

	// Adjust for possible DPI
	int realWidth = browserElements->widget->width();
	int realHeight = browserElements->widget->height();
	qreal scaleFactor = browserElements->widget->devicePixelRatioF();
	realWidth = static_cast<int>(realWidth * scaleFactor);
	realHeight = static_cast<int>(realHeight * scaleFactor);

	// Now set the parent of the CEF browser to the QWidget
	window_info.SetAsChild((HWND)browserElements->widget->winId(), CefRect(0, 0, realWidth, realHeight));

	// Create a unique request context for the new browser
	CefRequestContextSettings request_context_settings;
	CefRefPtr<CefRequestContext> request_context = CefRequestContext::CreateContext(request_context_settings, nullptr);

	static int32_t counter = 0;
	++counter;

	CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
	command_line->AppendSwitchWithValue("custom-arg", std::to_string(counter));

	browserElements->browser = CefBrowserHost::CreateBrowserSync(window_info, browserElements->client.get(), url, browser_settings, CefRefPtr<CefDictionaryValue>(), request_context);

	if (startHidden)
	{
		browserElements->widget->hide();
	}
	else
	{
		browserElements->widget->showNormal();

		if (keepOnTop)
		{
			std::thread(
				[](std::shared_ptr<BrowserElements> b) {
					// For the next second keep the window on top
					for (int i = 0; i < 10; ++i)
					{
						::SetForegroundWindow((HWND)b->widget->winId());
						::Sleep(100);
					}
				},
				browserElements)
				.detach();
		}
	}
}

void SlBrowser::browserInit()
{
	std::string version;
	std::string githubRevision;
	std::string revision;

#ifdef SL_OBS_VERSION
	version = SL_OBS_VERSION;
#else
	version = "debug";
#endif

#ifdef GITHUB_REVISION
	githubRevision = GITHUB_REVISION;
#else
	githubRevision = "debug";
#endif

#ifdef SL_REVISION
	revision = SL_REVISION;
#else
	revision = "debug";
#endif

	TCHAR moduleFileName[MAX_PATH]{};
	GetModuleFileName(NULL, moduleFileName, MAX_PATH);
	std::filesystem::path fsPath(moduleFileName);

	std::string path = fsPath.remove_filename().string();
	path += "sl-browser-page.exe";

	CefMainArgs args;

	CefSettings settings;
	settings.log_severity = LOGSEVERITY_VERBOSE;

	CefString(&settings.user_agent_product) = "Streamlabs";
	CefString(&settings.locale) = "en-US";
	CefString(&settings.accept_language_list) = "en-US,en";

	// Value that will be inserted as the product portion of the default
	// User-Agent string. If empty the Chromium product version will be used. If
	// |userAgent| is specified this value will be ignored. Also configurable
	// using the "user-agent-product" command-line switch.
	std::stringstream prod_ver;
	prod_ver << "Chrome/";
	prod_ver << std::to_string(cef_version_info(4)) << "." << std::to_string(cef_version_info(5)) << "." << std::to_string(cef_version_info(6)) << "." << std::to_string(cef_version_info(7));
	prod_ver << " SLABS/";
	prod_ver << revision << "." << version << "." << githubRevision;
	CefString(&settings.user_agent_product) = prod_ver.str();

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
		CrashHandler::instance().addLogfilePath(logPathFile);
	}

	// Set the remote debugging port
	settings.remote_debugging_port = 9123;

	m_app = new BrowserApp();

	CefExecuteProcess(args, m_app, nullptr);
	CefInitialize(args, settings, m_app, nullptr);

	// Register http://absolute/ scheme handler for older CEF builds which do not support file:// URLs
	CefRegisterSchemeHandlerFactory("http", "absolute", new BrowserSchemeHandlerFactory());
}

void SlBrowser::queueDestroyCefBrowser(const int32_t uid)
{
	std::lock_guard<std::mutex> g(m_mutex);

	m_lastError.clear();

	if (uid == 0)
	{
		m_lastError = "queueDestroyCefBrowser, param is 0, which is the main browser, which may not be destroyed.";
		return;
	}

	auto browserElements = m_browsers.find(uid);

	if (browserElements == m_browsers.end())
	{
		m_lastError = "queueDestroyCefBrowser, uid not found";
		return;
	}

	shared_ptr<BrowserElements> ptr = browserElements->second;

	if (ptr == nullptr)
	{
		m_lastError = "queueDestroyCefBrowser, internal error, the browser is null";
		return;
	}

	m_browsers.erase(browserElements);
	CefPostTask(TID_UI, base::BindOnce(&cleanupCefBrowser_Internal, ptr));
}

/*static*/
void SlBrowser::cleanupCefBrowser_Internal(std::shared_ptr<BrowserElements> browserElements)
{
	if (browserElements->browser)
	{
		CefRefPtr<CefBrowserHost> host = browserElements->browser->GetHost();

		if (host)
			host->CloseBrowser(true);

		browserElements->browser = nullptr;
	}

	if (browserElements->client)
		browserElements->client = nullptr;
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
	m_cefInit = true;
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

bool SlBrowser::getSavedHiddenState() const
{
	std::wstring filePath = getCacheDir() + L"\\window_state.txt";
	std::wifstream file(filePath);

	if (!file.is_open())
		return false;

	wchar_t ch;
	file >> ch;

	return ch == L'1';
}

int32_t SlBrowser::getBrowserCefId(const int32_t uid)
{
	if (auto ptr = getBrowserElements(uid))
		return ptr->browser->GetIdentifier();

	return 0;
}

std::shared_ptr<BrowserElements> SlBrowser::getBrowserElements(const int32_t uid)
{
	std::lock_guard<std::mutex> g(m_mutex);

	m_lastError.clear();

	auto browserElements = m_browsers.find(uid);

	if (browserElements == m_browsers.end())
	{
		m_lastError = "getBrowserElements, uid not found";
		return nullptr;
	}

	return browserElements->second;
}

std::string SlBrowser::popLastError()
{
	std::lock_guard<std::mutex> g(m_mutex);
	auto ret = m_lastError;
	m_lastError.clear();
	return ret;
}

void SlBrowser::saveHiddenState(const bool b) const
{
	namespace fs = std::filesystem;

	std::wstring cacheDir = getCacheDir();

	if (!fs::exists(cacheDir))
		fs::create_directories(cacheDir);

	std::wstring filePath = cacheDir + L"\\window_state.txt";
	std::wofstream file(filePath, std::ios::trunc);

	if (file.is_open())
		file << (b ? L'1' : L'0');
}

std::wstring SlBrowser::getCacheDir() const
{
	wchar_t path[MAX_PATH];

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
		return std::wstring(path) + L"\\StreamlabsOBS";

	return L"";
}

/*static*/
// todo: remove this?
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
			std::cout << SlBrowser::instance().m_mainBrowser->browser->GetIdentifier() << std::endl;
			SlBrowser::instance().m_mainBrowser->browser->GetMainFrame()->LoadURL(url);
		}
		else
		{
			std::cout << "URL cannot be empty. Please try again." << std::endl;
		}

		::Sleep(1000);
	}
}
