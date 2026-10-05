#include "SlBrowser.h"
#include "SlBrowserWidget.h"
#include "JavascriptApi.h"
#include "GrpcBrowser.h"
#include "CrashHandler.h"

#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include <algorithm>
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

	QApplication a(argc, argv);

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

	createCefBrowser(0, m_mainBrowser, SlBrowser::getDefaultUrl(), SlBrowser::instance().getSavedHiddenState(), true);

	std::thread(CheckForObsThread).detach();
	std::thread(DebugInputThread).detach();

	// Run Qt Application
	int result = a.exec();
}

/*static*/
std::string SlBrowser::getDefaultUrl()
{
	char buffer[MAX_PATH];
	DWORD len = GetEnvironmentVariableA("SL_PLUGIN_DEFAULT_URL", buffer, MAX_PATH);

	if (len > 0 && len < MAX_PATH)
		return buffer; 

	return "https://obs-plugin.streamlabs.com";
}

/*static*/
bool SlBrowser::isApprovedTabUrl(const std::string &url)
{
	CefURLParts parts;

	if (!CefParseURL(url, parts))
		return false;

	if (!CefString(&parts.username).empty() || !CefString(&parts.password).empty())
		return false;

	// The parsed origin is lowercased and drops a default port, whatever the url's own spelling
	std::string origin = CefString(&parts.origin).ToString();

	while (!origin.empty() && origin.back() == '/')
		origin.pop_back();

	if (origin.empty())
		return false;

	for (const char *allowed : JavascriptApi::kTabAllowedOrigins)
	{
		if (origin == allowed)
			return true;
	}

	static const std::string testOrigin = []() {
		char buffer[MAX_PATH];
		DWORD len = GetEnvironmentVariableA(JavascriptApi::kTestTabOriginEnvVar, buffer, MAX_PATH);
		std::string value = (len > 0 && len < MAX_PATH) ? std::string(buffer, len) : std::string();

		while (!value.empty() && value.back() == '/')
			value.pop_back();

		return value;
	}();

	return !testOrigin.empty() && origin == testOrigin;
}

// Empty on success, with the canonical path in resolved
std::string SlBrowser::resolveTabIconPath(const std::string &path, std::wstring &resolved) const
{
	namespace fs = std::filesystem;

	const std::string err = "icon must be an existing .png, .ico, .jpg or .jpeg file inside %APPDATA%\\StreamlabsOBS";

	const fs::path raw = fs::u8path(path);
	const std::wstring &native = raw.native();

	// UNC and \\?\ device paths: Qt would open them, and an SMB share would be sent the user's NTLM credentials
	if (native.rfind(L"\\\\", 0) == 0 || native.rfind(L"//", 0) == 0)
		return err;

	// A colon is only ever a drive designator; anywhere else it names an alternate data stream
	if (!raw.is_absolute() || native.find(L':', 2) != std::wstring::npos)
		return err;

	std::error_code ec;
	const fs::path root = fs::canonical(getCacheDir(), ec);

	if (ec)
		return err;

	const fs::path file = fs::canonical(raw, ec);

	if (ec || !fs::is_regular_file(file, ec))
		return err;

	auto root_it = root.begin();
	auto file_it = file.begin();

	for (; root_it != root.end(); ++root_it, ++file_it)
	{
		if (file_it == file.end() || _wcsicmp(root_it->c_str(), file_it->c_str()) != 0)
			return err;
	}

	if (file_it == file.end())
		return err;

	std::wstring ext = file.extension().wstring();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

	if (ext != L".png" && ext != L".ico" && ext != L".jpg" && ext != L".jpeg")
		return err;

	resolved = file.wstring();
	return "";
}

std::string SlBrowser::registerBrowser(const int32_t uuid, std::shared_ptr<BrowserElements> browserElements)
{
	std::lock_guard<std::mutex> g(m_mutex);

	if (m_browsers.find(uuid) != m_browsers.end())
		return "uuid already exists";

	browserElements->uid = uuid;
	m_browsers[uuid] = browserElements;
	return "";
}

void SlBrowser::createCefBrowser(const int32_t uuid, std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop)
{
	const std::string err = registerBrowser(uuid, browserElements);

	if (!err.empty())
	{
		printf("sl-proxy: createCefBrowser, %s\n", err.c_str());
		return;
	}

	browserElements->widget->setElements(browserElements);

	CefPostTask(TID_UI, base::BindOnce(&createCefBrowser_internal, browserElements, url, startHidden, keepOnTop));
}

std::string SlBrowser::createTabWindow(const int32_t uid, const std::string &url, TabWindowOptions options)
{
	if (uid == 0)
		return "uid 0 is the main browser";

	// Reserved now so a duplicate uid is rejected in the reply; the widget has to be built on the Qt thread
	auto elements = std::make_shared<BrowserElements>();
	elements->initScript = std::move(options.initScript);
	elements->hideOnClose = options.hideOnClose;
	elements->onCreated = std::move(options.onCreated);
	const std::string err = registerBrowser(uid, elements);

	if (!err.empty())
		return err;

	QMetaObject::invokeMethod(
		qApp,
		[elements, url, title = std::move(options.title), iconPath = std::move(options.iconPath)]() {
			elements->widget = new SlBrowserWidget;
			elements->widget->setElements(elements);
			elements->widget->setWindowTitle(title.c_str());
			elements->widget->setMinimumSize(320, 240);
			elements->widget->resize(1280, 720);

			if (!iconPath.empty())
				elements->widget->window()->setWindowIcon(QIcon(QString::fromStdWString(iconPath)));

			// The HWND is not made until the widget is shown at least once
			elements->widget->showMinimized();

			CefPostTask(TID_UI, base::BindOnce(&createCefBrowser_internal, elements, url, false, false));
		},
		Qt::QueuedConnection);

	return "";
}

/*static*/
void SlBrowser::createCefBrowser_internal(std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop)
{
	CefWindowInfo window_info;
	CefBrowserSettings browser_settings;
	browserElements->client = new BrowserClient(false);
	browserElements->client->SetIsMain(browserElements->uid == 0);

	// Adjust for possible DPI
	int realWidth = browserElements->widget->width();
	int realHeight = browserElements->widget->height();
	qreal scaleFactor = browserElements->widget->devicePixelRatioF();
	realWidth = static_cast<int>(realWidth * scaleFactor);
	realHeight = static_cast<int>(realHeight * scaleFactor);

	// Now set the parent of the CEF browser to the QWidget
	window_info.SetAsChild((HWND)browserElements->widget->winId(), CefRect(0, 0, realWidth, realHeight));

	// Tabs share the global request context, and so share cookies and login with the main browser
	CefRefPtr<CefRequestContext> request_context = CefRequestContext::GetGlobalContext();

	// Reaches the renderer's OnBrowserCreated, which runs the script in every main-frame document of the tab
	CefRefPtr<CefDictionaryValue> extra_info;

	if (!browserElements->initScript.empty())
	{
		extra_info = CefDictionaryValue::Create();
		extra_info->SetString("initScript", browserElements->initScript);
	}

	browserElements->browser = CefBrowserHost::CreateBrowserSync(window_info, browserElements->client.get(), url, browser_settings, extra_info, request_context);

	if (!browserElements->browser)
	{
		if (browserElements->onCreated)
			browserElements->onCreated("failed to create the browser");

		browserElements->onCreated = nullptr;
		SlBrowser::instance().queueDestroyCefBrowser(browserElements->uid);
		return;
	}

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

	// The main widget was shown before it had elements to report to
	browserElements->hidden = browserElements->widget->isHidden();
	browserElements->ready = true;

	if (browserElements->onCreated)
		browserElements->onCreated("");

	browserElements->onCreated = nullptr;
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

std::string SlBrowser::queueDestroyCefBrowser(const int32_t uid)
{
	std::lock_guard<std::mutex> g(m_mutex);

	if (uid == 0)
		return "uid 0 is the main browser, which may not be destroyed";

	auto browserElements = m_browsers.find(uid);

	if (browserElements == m_browsers.end())
		return "uid not found";

	std::shared_ptr<BrowserElements> ptr = browserElements->second;

	// The widget is released with the elements, after the CEF browser is closed
	QMetaObject::invokeMethod(
		qApp,
		[ptr]() {
			if (ptr->widget)
				ptr->widget->hide();

			CefPostTask(TID_UI, base::BindOnce(&cleanupCefBrowser_Internal, ptr));
		},
		Qt::QueuedConnection);

	m_browsers.erase(browserElements);
	return "";
}

void SlBrowser::closeTabWindow(const int32_t uid)
{
	if (!queueDestroyCefBrowser(uid).empty())
		return;

	if (m_mainBrowser == nullptr)
		return;

	CefPostTask(TID_UI, base::BindOnce(&BrowserClient::SendMsgToReceiver, m_mainBrowser->browser, std::string(JavascriptApi::kTabClosedMessage), uid));
}

/*static*/
void SlBrowser::cleanupCefBrowser_Internal(std::shared_ptr<BrowserElements> browserElements)
{
	if (browserElements->browser)
	{
		if (auto frame = browserElements->browser->GetMainFrame())
			frame->LoadURL("about:blank");

		if (browserElements->client)
			browserElements->client->RemoveBrowserFromCallback(browserElements->browser);

		browserElements->browser->GetHost()->CloseBrowser(true);
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

int32_t SlBrowser::getUuidFromCefId(const int32_t cefId)
{
	std::lock_guard<std::mutex> g(m_mutex);

	for (auto &itr : m_browsers)
	{
		if (itr.second && itr.second->ready && itr.second->browser && itr.second->browser->GetIdentifier() == cefId)
			return itr.first;
	}

	return 0;
}

int32_t SlBrowser::getBrowserCefId(const int32_t uid)
{
	if (auto ptr = getBrowserElements(uid))
	{
		if (ptr->ready && ptr->browser)
			return ptr->browser->GetIdentifier();
	}

	return 0;
}

std::map<int32_t, std::shared_ptr<BrowserElements>> SlBrowser::getExtraBrowsers()
{
	std::lock_guard<std::mutex> g(m_mutex);

	auto extra = m_browsers;
	extra.erase(0);
	return extra;
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
			std::cout << ";" << std::endl;
			SlBrowser::instance().m_mainBrowser->browser->GetMainFrame()->LoadURL(url);
		}
		else
		{
			std::cout << "URL cannot be empty. Please try again." << std::endl;
		}

		::Sleep(1000);
	}
}
