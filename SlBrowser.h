#pragma once

#include "browser-client.hpp"
#include "browser-app.hpp"
#include "SlBrowserWidget.h"

#include <QWidget>
#include <atomic>
#include <functional>
#include <map>

// What a tab window is made with, beyond its uid and url
struct TabWindowOptions
{
	std::string title;
	std::string iconPath;
	std::string initScript;

	// Called on the CEF UI thread once the browser exists, or has failed to
	std::function<void(const std::string &err)> onCreated;
};

struct BrowserElements
{
	~BrowserElements();
	int32_t uid = 0;
	SlBrowserWidget *widget = nullptr;
	CefRefPtr<CefBrowser> browser = nullptr;
	CefRefPtr<BrowserClient> client = nullptr;

	// Set once widget and browser both exist. The widget is made on the Qt thread and the browser on the CEF UI thread, so neither can be trusted before this
	std::atomic<bool> ready = false;

	// Mirrors widget->isHidden() so it can be read from any thread
	std::atomic<bool> hidden = true;

	std::string initScript;
	std::function<void(const std::string &err)> onCreated;

	static void queueCleanupQtObj(QWidget *widget)
	{
		if (widget != nullptr)
		{
			QMetaObject::invokeMethod(widget, "deleteLater", Qt::QueuedConnection);
		}
	}
};

class SlBrowser
{
public:
	SlBrowser(const SlBrowser &) = delete;
	SlBrowser &operator=(const SlBrowser &) = delete;

public:
	void run(int argc, char *argv[]);
	static std::string getDefaultUrl();
	std::string createTabWindow(const int32_t uid, const std::string &url, TabWindowOptions options);
	std::string queueDestroyCefBrowser(const int32_t uuid);
	void closeTabWindow(const int32_t uid);
	void setMainPageSuccess(const bool b) { m_mainPageSuccess = b; }
	void setMainLoadingInProgress(const bool b) { m_mainLoadingInProgress = b; }
	void saveHiddenState(const bool b) const;
	void createCefBrowser(const int32_t uuid, std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop);

	bool getSavedHiddenState() const;
	bool getMainPageSuccess() const { return m_mainPageSuccess; }
	bool getMainLoadingInProgress() const { return m_mainLoadingInProgress; }

	int32_t getBrowserCefId(const int32_t uid);
	int32_t getUuidFromCefId(const int32_t cefId);

	std::shared_ptr<BrowserElements> getBrowserElements(const int32_t uid);
	std::map<int32_t, std::shared_ptr<BrowserElements>> getExtraBrowsers();

	std::string popLastError();

public:
	bool m_allowHideBrowser = true;
	int32_t m_obs64_PIDt = 0;
	std::atomic<bool> m_cefInit = false;
	CefRefPtr<BrowserApp> m_app = nullptr;
	std::shared_ptr<BrowserElements> m_mainBrowser = nullptr;
	std::map<int32_t, std::shared_ptr<BrowserElements>> m_browsers;

public:
	static SlBrowser &instance()
	{
		static SlBrowser instance;
		return instance;
	}

private:
	SlBrowser();
	~SlBrowser();

	void browserInit();
	void browserShutdown();
	void browserManagerThread();

	std::string registerBrowser(const int32_t uuid, std::shared_ptr<BrowserElements> browserElements);

	static void createCefBrowser_internal(std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop);
	static void cleanupCefBrowser_Internal(std::shared_ptr<BrowserElements> browserElements);

	std::wstring getCacheDir() const;

	static void DebugInputThread();
	static void CheckForObsThread();

	bool m_mainPageSuccess = false;
	bool m_mainLoadingInProgress = false;
	bool m_cefCreated = false;

	std::mutex m_mutex;
	std::string m_lastError;
};
