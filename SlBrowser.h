#pragma once

#include "browser-client.hpp"
#include "browser-app.hpp"
#include "URL.h"

#include <QWidget>
#include <map>

struct BrowserElements
{
	~BrowserElements();
	int32_t uid = 0;
	QWidget* widget = nullptr;
	CefRefPtr<CefBrowser> browser = nullptr;
	CefRefPtr<BrowserClient> client = nullptr;

	static void queueCleanupQtObj(QWidget *widget) { if (widget != nullptr) { QMetaObject::invokeMethod(widget, "deleteLater", Qt::QueuedConnection); } }
};

class SlBrowser
{
public:
	SlBrowser(const SlBrowser &) = delete;
	SlBrowser &operator=(const SlBrowser &) = delete;

public:
	void run(int argc, char *argv[]);
	void createAppStore();
	void queueDestroyCefBrowser(const int32_t uuid);
	void setMainPageSuccess(const bool b) { m_mainPageSuccess = b; }
	void setMainLoadingInProgress(const bool b) { m_mainLoadingInProgress = b; }
	void saveHiddenState(const bool b) const;
	void createCefBrowser(const int32_t uuid, std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop);

	bool getSavedHiddenState() const;
	bool getMainPageSuccess() const { return m_mainPageSuccess; }
	bool getMainLoadingInProgress() const { return m_mainLoadingInProgress; }

	int32_t getBrowserCefId(const int32_t uid);

	std::shared_ptr<BrowserElements> getBrowserElements(const int32_t uid);
	const std::map<int32_t, std::shared_ptr<BrowserElements>> &getExtraBrowsers() const { return m_browsers; }

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
	std::unique_ptr<QApplication> m_qapp = nullptr;
};
