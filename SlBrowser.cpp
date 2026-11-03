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
#include <vector>
#include <ctime>

#include <QTimer>
#include <QPointer>
#include <QVBoxLayout>
#include <QDialog>
#include <QMainWindow>
#include <QAbstractButton>
#include <QPushButton>
#include <QApplication>

#include "browser-version.h"
#include "json11/json11.hpp"
#include "cef-headers.hpp"
#if __has_include(<include/cef_version_info.h>)
#include <include/cef_version_info.h>
#endif
#include "ConsoleToggle.h"

#include <include/base/cef_callback.h>
#include <include/base/cef_logging.h>
#include <include/wrapper/cef_closure_task.h>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <ShlObj.h>
#include <iostream>

using namespace std;
using namespace json11;

static constexpr UINT kCefInitFailedExitBase = 1000;

SlBrowser::SlBrowser() {}

SlBrowser::~SlBrowser() {}

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

	// Create Qt Widget
	m_widget = new SlBrowserWidget{};
	m_widget->setWindowTitle("Streamlabs");
	m_widget->setMinimumSize(320, 240);
	m_widget->resize(1280, 720);

	// We have to show before creating CEF because it needs the HWND, and the HWND is not made until the QtWidget is shown at least once
	m_widget->showMinimized();

	CefPostTask(TID_UI, base::BindOnce(&CreateCefBrowser, 5));

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

void SlBrowser::CreateCefBrowser(int arg)
{
	auto &app = SlBrowser::instance();

	CefWindowInfo window_info;
	CefBrowserSettings browser_settings;
	app.browserClient = new BrowserClient(false);

	CefString url = SlBrowser::getDefaultUrl();

	// Adjust for possible DPI 
	int realWidth = app.m_widget->width();
	int realHeight = app.m_widget->height();
	qreal scaleFactor = app.m_widget->devicePixelRatioF();
	realWidth = static_cast<int>(realWidth * scaleFactor);
	realHeight = static_cast<int>(realHeight * scaleFactor);

	// Now set the parent of the CEF browser to the QWidget
	window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
	window_info.SetAsChild((HWND)app.m_widget->winId(), CefRect(0, 0, realWidth, realHeight));
	app.m_browser = CefBrowserHost::CreateBrowserSync(window_info, app.browserClient.get(), url, browser_settings, CefRefPtr<CefDictionaryValue>(), nullptr);

	if (SlBrowser::instance().getSavedHiddenState())
	{
		SlBrowser::instance().m_widget->hide();
	}
	else
	{
		SlBrowser::instance().m_widget->showNormal();

		auto bringToTop = []
		{
			// For the next second keep the window on top
			for (int i = 0; i < 10; ++i)
			{
				::SetForegroundWindow((HWND)SlBrowser::instance().m_widget->winId());
				::Sleep(100);
			}
		};

		std::thread(bringToTop).detach();
	}
}

#if CHROME_VERSION_BUILD > 6533
namespace
{
struct ProfileMigrationLock
{
	HANDLE handle = CreateMutexW(nullptr, FALSE, L"Local\\StreamlabsOBS_CEF_Cache_ProfileMigration");
	bool acquired = false;

	ProfileMigrationLock()
	{
		if (!handle)
			return;

		DWORD wait = WaitForSingleObject(handle, 30000);
		acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
	}

	~ProfileMigrationLock()
	{
		if (!handle)
			return;

		if (acquired)
			ReleaseMutex(handle);

		CloseHandle(handle);
	}
};

bool needsProfileMigration(const std::filesystem::path &root, std::string *reason = nullptr)
{
	std::error_code ec;
	if (std::filesystem::exists(root / "Default", ec))
	{
		if (reason)
			*reason = "Default profile already exists";
		return false;
	}

	if (!std::filesystem::exists(root / "LocalPrefs.json", ec))
	{
		if (reason)
			*reason = "no LocalPrefs.json, nothing to migrate";
		return false;
	}

	return true;
}

std::string describeError(const std::error_code &ec)
{
	return "error " + std::to_string(ec.value()) + ": " + ec.message();
}

constexpr size_t kMigrationLogMaxBytes = 64 * 1024;

void appendMigrationLog(const std::filesystem::path &root, const std::vector<std::string> &lines)
{
	std::string content;
	{
		std::ifstream in(root / "profile-migration.log", std::ios::binary);
		if (in)
			content.assign(std::istreambuf_iterator<char>(in), {});
	}

	std::time_t now = std::time(nullptr);
	std::tm tm{};
	localtime_s(&tm, &now);
	char stamp[32]{};
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

	for (const auto &line : lines)
		content += std::string(stamp) + " " + line + "\n";

	if (content.size() > kMigrationLogMaxBytes)
	{
		size_t cut = content.size() - kMigrationLogMaxBytes / 2;
		size_t nl = content.find('\n', cut);
		content.erase(0, nl == std::string::npos ? cut : nl + 1);
	}

	std::ofstream out(root / "profile-migration.log", std::ios::binary | std::ios::trunc);
	out << content;
}
} // namespace

// Chrome-runtime CEF only reads the "Default" profile; copy the pre-Chrome root layout into it.
static void migrateToChromeProfileLayout(const std::filesystem::path &root, std::vector<std::string> &log)
{
	namespace fs = std::filesystem;

	std::vector<std::string> persistent;
	auto record = [&](const std::string &line, bool persist) {
		log.push_back(line);
		if (persist)
			persistent.push_back(line);
	};
	const std::string fallback = " - CEF will start with a fresh profile; the old data is still in " + root.string() + " and can be recovered manually";
	auto fail = [&](const std::string &step, const fs::path &path, const std::error_code &ec) {
		record("Profile migration failed at " + step + " (" + path.string() + "), " + describeError(ec) + fallback, true);
		appendMigrationLog(root, persistent);
	};

	std::string reason;
	if (!needsProfileMigration(root, &reason))
	{
		record("Profile migration not needed: " + reason, false);
		return;
	}

	ProfileMigrationLock lock;
	if (!lock.acquired)
	{
		if (lock.handle)
			record("Profile migration skipped: lock not acquired within 30s" + fallback, true);
		else
			record("Profile migration skipped: could not create lock, error " + std::to_string(GetLastError()) + fallback, true);

		appendMigrationLog(root, persistent);
		return;
	}

	if (!needsProfileMigration(root, &reason))
	{
		record("Profile migration not needed: " + reason + " (migrated by another instance)", false);
		return;
	}

	const std::string suffix = ".migrating-" + std::to_string(GetCurrentProcessId());
	const fs::path state = root / "Local State";
	const fs::path stateStaging = root / ("Local State" + suffix);
	const fs::path profile = root / "Default";
	const fs::path profileStaging = root / ("Default" + suffix);
	std::error_code ec;

	// Local State holds the key that decrypts the cookies, so it goes first.
	if (!fs::exists(state, ec))
	{
		fs::copy_file(root / "LocalPrefs.json", stateStaging, fs::copy_options::overwrite_existing, ec);
		if (!ec && !MoveFileExW(stateStaging.c_str(), state.c_str(), 0) && GetLastError() != ERROR_ALREADY_EXISTS)
			ec = std::error_code(GetLastError(), std::system_category());

		if (ec)
		{
			std::error_code cleanupEc;
			fs::remove(stateStaging, cleanupEc);
			fail("Local State", root / "LocalPrefs.json", ec);
			return;
		}

		fs::remove(stateStaging, ec);
	}

	fs::remove_all(profileStaging, ec);
	fs::create_directories(profileStaging, ec);

	std::string failedStep = "creating Default staging directory";
	fs::path failedPath = profileStaging;
	for (const char *name : {"Local Storage", "Session Storage", "Network"})
	{
		if (ec)
			break;

		failedStep = name;
		failedPath = root / name;
		if (fs::exists(root / name, ec))
			fs::copy(root / name, profileStaging / name, fs::copy_options::recursive, ec);
	}

	if (!ec && !MoveFileExW(profileStaging.c_str(), profile.c_str(), 0) && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		ec = std::error_code(GetLastError(), std::system_category());
		failedStep = "moving Default into place";
		failedPath = profile;
	}

	if (ec)
	{
		std::error_code cleanupEc;
		fs::remove_all(profileStaging, cleanupEc);
		fail(failedStep, failedPath, ec);
		return;
	}

	// A lost race leaves our staging dir behind; the winner's Default is already in place.
	std::error_code cleanupEc;
	fs::remove_all(profileStaging, cleanupEc);
	record("Profile migration succeeded: copied Local State, Local Storage, Session Storage and Network into Default", true);
	appendMigrationLog(root, persistent);
}
#endif

bool SlBrowser::browserInit()
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

#if CHROME_VERSION_BUILD <= 6533
	settings.persist_user_preferences = 1;
#endif

	char cache_path[MAX_PATH];
	std::string cache_pathStdStr;

	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, cache_path)))
	{
		cache_pathStdStr = std::string(cache_path) + "\\StreamlabsOBS_CEF_Cache";
		CefString(&settings.cache_path) = cache_pathStdStr;
#if CHROME_VERSION_BUILD > 6533
		CefString(&settings.root_cache_path) = cache_pathStdStr;
#endif
	}

	CefString(&settings.browser_subprocess_path) = path.c_str();

	if (!cache_pathStdStr.empty())
	{
		std::string logPathFile = cache_pathStdStr + "\\cef.log";
		CefString(&settings.log_file) = logPathFile;
		settings.log_severity = LOGSEVERITY_DEBUG;
		CrashHandler::instance().addLogfilePath(logPathFile);
	}

	if (int port = BrowserApp::getRemoteDebuggingPort())
	{
		settings.remote_debugging_port = port;
		std::cout << "Remote debugging enabled on port " << port << std::endl;
	}
	else
	{
		char value[32];
		if (GetEnvironmentVariableA("SL_PLUGIN_REMOTE_DEBUGGING_PORT", value, sizeof(value)) > 0)
			std::cout << "Warning: SL_PLUGIN_REMOTE_DEBUGGING_PORT must be a port from 1024 to 65535; remote debugging disabled" << std::endl;
		else
			std::cout << "Remote debugging disabled" << std::endl;
	}

	m_app = new BrowserApp();

	CefExecuteProcess(args, m_app, nullptr);

	std::vector<std::string> migrationLog;
#if CHROME_VERSION_BUILD > 6533
	if (!cache_pathStdStr.empty())
		migrateToChromeProfileLayout(cache_pathStdStr, migrationLog);
#endif

	if (!CefInitialize(args, settings, m_app, nullptr))
		return false;

	// CEF logging is only live after CefInitialize
	for (const auto &line : migrationLog)
		LOG(INFO) << line;

	return true;
}

void SlBrowser::browserShutdown()
{
	CefClearSchemeHandlerFactories();
	CefShutdown();
	m_app = nullptr;
}

void SlBrowser::browserManagerThread()
{
	if (!browserInit())
	{
		// Distinct from abort()'s exit code 3; the plugin logs it
		int cefExitCode = CefGetExitCode();
		printf("sl-browser: CefInitialize failed, CEF exit code = %d\n", cefExitCode);
		ExitProcess(kCefInitFailedExitBase + cefExitCode);
	}

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
			SlBrowser::instance().m_browser->GetMainFrame()->LoadURL(url);
		}
		else
		{
			std::cout << "URL cannot be empty. Please try again." << std::endl;
		}

		::Sleep(1000);
	}
}
