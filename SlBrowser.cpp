#include "SlBrowser.h"
#include "SlBrowserWidget.h"
#include "JavascriptApi.h"
#include "GrpcBrowser.h"
#include "CrashHandler.h"
#include "WindowsFunctions.h"

#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
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
	m_mainBrowser->isMain = true;
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
std::string SlBrowser::getApiOrigin()
{
	return BrowserApp::OriginOf(getDefaultUrl());
}

/*static*/
bool SlBrowser::isApprovedTabUrl(const std::string &url)
{
	return BrowserApp::isApprovedTabUrl(url);
}

// Empty when the key is empty or too long. Every byte outside [a-z0-9-] is escaped, so no two keys share a directory, even on a case-insensitive disk, and nothing can climb out of it
/*static*/
std::string SlBrowser::tabContextDirName(const std::string &key)
{
	if (key.empty() || key.size() > JavascriptApi::kMaxTabContextKeyBytes)
		return "";

	std::string name = "app-";

	for (const unsigned char c : key)
	{
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
		{
			name += static_cast<char>(c);
		}
		else
		{
			char escaped[4];
			snprintf(escaped, sizeof(escaped), "_%02x", c);
			name += escaped;
		}
	}

	return name;
}

namespace
{
constexpr const char *kClosedBeforeCreated = "the window was closed before it was created";

// Answers tabs_createWindow, once. CEF UI thread only
void replyCreated(const std::shared_ptr<BrowserElements> &elements, const std::string &err)
{
	if (auto onCreated = std::move(elements->onCreated))
		onCreated(err);

	elements->onCreated = nullptr;
}

void finishBrowserCreation(std::shared_ptr<BrowserElements> elements)
{
	if (elements->closed)
	{
		replyCreated(elements, kClosedBeforeCreated);
		return;
	}

	elements->ready = true;
	replyCreated(elements, "");
}

// Zero when unset
int testEnvMs(const char *name)
{
	char buffer[32];
	DWORD len = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
	return (len > 0 && len < sizeof(buffer)) ? atoi(buffer) : 0;
}

void onProfileTimeout(std::weak_ptr<BrowserElements> weak)
{
	auto elements = weak.lock();

	if (!elements || elements->closed || elements->browser)
		return;

	replyCreated(elements, "the tab's profile did not initialize in time");
	SlBrowser::instance().queueDestroyCefBrowser(elements->uid, elements);
}

// On-disk profiles are made asynchronously, so a browser can only be attached once the context says it is initialized. Touched on the CEF UI thread only
struct TabProfile
{
	CefRefPtr<CefRequestContext> context;
	bool initialized = false;
	std::vector<std::function<void(CefRefPtr<CefRequestContext>)>> waiters;
};

std::map<std::string, TabProfile> g_tabProfiles;

void tabProfileInitialized(const std::string &contextDir, CefRefPtr<CefRequestContext> context)
{
	TabProfile &profile = g_tabProfiles[contextDir];
	profile.context = context;
	profile.initialized = true;

	auto waiters = std::move(profile.waiters);
	profile.waiters.clear();

	for (auto &waiter : waiters)
		waiter(context);
}

class TabProfileHandler : public CefRequestContextHandler
{
public:
	explicit TabProfileHandler(const std::string &contextDir) : m_contextDir(contextDir) {}

	void OnRequestContextInitialized(CefRefPtr<CefRequestContext> request_context) override
	{
		static const int delayMs = testEnvMs(JavascriptApi::kTestTabProfileDelayEnvVar);

		if (delayMs > 0 && m_contextDir.rfind(JavascriptApi::kTestTabProfileDelayDirPrefix, 0) == 0)
			CefPostDelayedTask(TID_UI, base::BindOnce(&tabProfileInitialized, m_contextDir, request_context), delayMs);
		else
			tabProfileInitialized(m_contextDir, request_context);
	}

private:
	std::string m_contextDir;

	IMPLEMENT_REFCOUNTING(TabProfileHandler);
};
} // namespace

// A tab with a context directory gets a profile on disk directly under the cache root (Chrome runtime treats a nested cache path as incognito), shared by every tab using it. Without one, or without a cache root, it gets a private in-memory profile
void SlBrowser::withTabRequestContext(const std::string &contextDir, std::function<void(CefRefPtr<CefRequestContext>)> onReady) const
{
	if (contextDir.empty() || m_cefCachePath.empty())
	{
		onReady(CefRequestContext::CreateContext(CefRequestContextSettings(), nullptr));
		return;
	}

	auto found = g_tabProfiles.find(contextDir);

	if (found != g_tabProfiles.end())
	{
		if (found->second.initialized)
			onReady(found->second.context);
		else
			found->second.waiters.push_back(std::move(onReady));

		return;
	}

	g_tabProfiles[contextDir];

	CefRequestContextSettings settings;
	CefString(&settings.cache_path) = m_cefCachePath + "\\" + contextDir;
	CefRequestContext::CreateContext(settings, new TabProfileHandler(contextDir));

	TabProfile &profile = g_tabProfiles[contextDir];

	if (profile.initialized)
		onReady(profile.context);
	else
		profile.waiters.push_back(std::move(onReady));
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
	elements->contextDir = std::move(options.contextDir);
	elements->onCreated = std::move(options.onCreated);
	const std::string err = registerBrowser(uid, elements);

	if (!err.empty())
		return err;

	QMetaObject::invokeMethod(
		qApp,
		[elements, url, title = std::move(options.title), iconPath = std::move(options.iconPath)]() {
			if (elements->closed)
				return;

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
	if (browserElements->closed)
	{
		replyCreated(browserElements, kClosedBeforeCreated);
		return;
	}

	browserElements->client = new BrowserClient(false);
	browserElements->client->SetIsMain(browserElements->isMain);
	browserElements->client->SetElements(browserElements);

	// Only main uses the global context; a tab sharing it would share cookies and login with main
	if (browserElements->isMain)
	{
		attachCefBrowser(browserElements, url, startHidden, keepOnTop, CefRequestContext::GetGlobalContext());
		return;
	}

	// Only the registry owns a tab that is still waiting, so one destroyed meanwhile is simply gone
	std::weak_ptr<BrowserElements> weak = browserElements;

	SlBrowser::instance().withTabRequestContext(browserElements->contextDir, [weak, url, startHidden, keepOnTop](CefRefPtr<CefRequestContext> request_context) {
		if (auto elements = weak.lock())
			attachCefBrowser(elements, url, startHidden, keepOnTop, request_context);
	});

	static const int timeoutMs = []() {
		const int fromEnv = testEnvMs(JavascriptApi::kTestTabProfileTimeoutEnvVar);
		return fromEnv > 0 ? fromEnv : JavascriptApi::kTabProfileTimeoutMs;
	}();

	CefPostDelayedTask(TID_UI, base::BindOnce(&onProfileTimeout, weak), timeoutMs);
}

/*static*/
void SlBrowser::attachCefBrowser(std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop, CefRefPtr<CefRequestContext> request_context)
{
	if (browserElements->closed)
	{
		replyCreated(browserElements, kClosedBeforeCreated);
		return;
	}

	// A keyed profile may have been initializing while the widget was resized.
	// Capture its native handle and current geometry on Qt's thread, then return to CEF.
	QMetaObject::invokeMethod(
		qApp,
		[browserElements, url, startHidden, keepOnTop, request_context]() {
			if (browserElements->closed)
			{
				CefPostTask(TID_UI, base::BindOnce(&finishBrowserCreation, browserElements));
				return;
			}

			auto *widget = browserElements->widget;
			const qreal scaleFactor = widget->devicePixelRatioF();
			const int width = static_cast<int>(widget->width() * scaleFactor);
			const int height = static_cast<int>(widget->height() * scaleFactor);
			const CefWindowHandle parentWindow = (CefWindowHandle)widget->winId();
			CefPostTask(TID_UI, base::BindOnce(&attachCefBrowserWithWindow, browserElements, url, startHidden, keepOnTop, request_context, parentWindow, width, height));
		},
		Qt::QueuedConnection);
}

/*static*/
void SlBrowser::attachCefBrowserWithWindow(std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop, CefRefPtr<CefRequestContext> request_context, CefWindowHandle parentWindow, int width, int height)
{
	if (browserElements->closed)
	{
		replyCreated(browserElements, kClosedBeforeCreated);
		return;
	}

	CefWindowInfo window_info;
	CefBrowserSettings browser_settings;
	window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
	window_info.SetAsChild(parentWindow, CefRect(0, 0, width, height));

	// Reaches the renderer's OnBrowserCreated: the role decides what it binds, and the script runs in every main-frame document of the tab
	CefRefPtr<CefDictionaryValue> extra_info = CefDictionaryValue::Create();
	extra_info->SetString("role", browserElements->isMain ? "main" : "tab");

	if (!browserElements->initScript.empty())
		extra_info->SetString("initScript", browserElements->initScript);

	browserElements->browser = CefBrowserHost::CreateBrowserSync(window_info, browserElements->client.get(), url, browser_settings, extra_info, request_context);

	if (!browserElements->browser)
	{
		replyCreated(browserElements, "failed to create the browser");
		SlBrowser::instance().queueDestroyCefBrowser(browserElements->uid);
		return;
	}

	QMetaObject::invokeMethod(
		qApp,
		[browserElements, startHidden, keepOnTop]() {
			if (!browserElements->closed)
			{
				auto *widget = browserElements->widget;
				if (startHidden)
					widget->hide();
				else
				{
					widget->showNormal();

					if (keepOnTop && !WindowsFunctions::TestOffscreen())
					{
						// Keep the main window in front briefly without touching its widget from a worker thread.
						std::weak_ptr<BrowserElements> weak = browserElements;
						for (int i = 0; i < 10; ++i)
							QTimer::singleShot(i * 100, widget, [weak, widget]() {
								if (auto elements = weak.lock(); elements && !elements->closed)
									::SetForegroundWindow((HWND)widget->winId());
							});
					}
				}

				// The main widget was shown before it had elements to report to.
				browserElements->hidden = widget->isHidden();
			}

			CefPostTask(TID_UI, base::BindOnce(&finishBrowserCreation, browserElements));
		},
		Qt::QueuedConnection);
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

	m_cefCachePath = cache_pathStdStr;

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

	m_app = new BrowserApp(getApiOrigin());

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

std::string SlBrowser::queueDestroyCefBrowser(const int32_t uid, const std::shared_ptr<BrowserElements> &expected, bool notifyClosed)
{
	std::lock_guard<std::mutex> g(m_mutex);

	if (uid == 0)
		return "uid 0 is the main browser, which may not be destroyed";

	auto browserElements = m_browsers.find(uid);

	if (browserElements == m_browsers.end())
		return "uid not found";

	if (expected && browserElements->second != expected)
		return "uid not found";

	std::shared_ptr<BrowserElements> ptr = browserElements->second;
	if (notifyClosed)
		ptr->notifyClosed = true;
	ptr->closed = true;

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

bool SlBrowser::closeTabWindow(const int32_t uid, const std::shared_ptr<BrowserElements> &expected)
{
	return queueDestroyCefBrowser(uid, expected, true).empty();
}

namespace
{
constexpr int kCloseFallbackMs = 2000;

// The elements stay alive, and with them the widget, until the browser has finished closing
using CloseHold = std::shared_ptr<std::shared_ptr<BrowserElements>>;

void releaseCloseHold(CloseHold hold)
{
	hold->reset();
}
} // namespace

/*static*/
void SlBrowser::cleanupCefBrowser_Internal(std::shared_ptr<BrowserElements> browserElements)
{
	replyCreated(browserElements, kClosedBeforeCreated);

	// Only a tab that was told it exists is told it is gone
	const bool notifyClosed = browserElements->notifyClosed && browserElements->ready;
	CefRefPtr<CefBrowser> browser = std::move(browserElements->browser);
	CefRefPtr<BrowserClient> client = std::move(browserElements->client);
	browserElements->browser = nullptr;
	browserElements->client = nullptr;

	if (browser && browser->IsValid())
	{
		// Its own root window, so closing it can never be taken for a close of the Qt widget
		// Null when the page already closed it itself
		if (HWND hwnd = browser->GetHost()->GetWindowHandle())
		{
			::ShowWindow(hwnd, SW_HIDE);
			::SetParent(hwnd, nullptr);
		}

		auto hold = std::make_shared<std::shared_ptr<BrowserElements>>(browserElements);
		client->SetOnBeforeClose([hold]() { releaseCloseHold(hold); });
		browser->GetHost()->CloseBrowser(true);

		CefPostDelayedTask(TID_UI, base::BindOnce(&releaseCloseHold, hold), kCloseFallbackMs);
	}

	const auto &mainBrowser = SlBrowser::instance().m_mainBrowser;

	if (notifyClosed && mainBrowser && mainBrowser->browser)
		BrowserClient::SendMsgToReceiver(mainBrowser->browser, JavascriptApi::kTabClosedMessage, browserElements->uid);
}

void SlBrowser::browserShutdown()
{
	g_tabProfiles.clear();
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

int32_t SlBrowser::getUuidFromCefId(const int32_t cefId)
{
	std::lock_guard<std::mutex> g(m_mutex);

	for (auto &itr : m_browsers)
	{
		if (itr.second && itr.second->ready && itr.second->browser && itr.second->browser->GetIdentifier() == cefId)
			return itr.first;
	}

	return -1;
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
