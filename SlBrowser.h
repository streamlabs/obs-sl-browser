#pragma once

#include "browser-client.hpp"
#include "browser-app.hpp"

#include <QWidget>

struct BrowserElements
{
	std::shared_ptr<QWidget> widget = nullptr;
	CefRefPtr<CefBrowser> browser = nullptr;
	CefRefPtr<BrowserClient> client = nullptr;
};

class SlBrowser
{
public:
	void run(int argc, char *argv[]);
	void createAppStore();
	void cleanupCefBrowser(std::shared_ptr<BrowserElements> browserElements);

	bool getSavedHiddenState() const;
	void saveHiddenState(const bool b) const;

	bool getMainPageSuccess() const { return m_mainPageSuccess; }
	bool getMainLoadingInProgress() const { return m_mainLoadingInProgress; }

	void setMainPageSuccess(const bool b) { m_mainPageSuccess = b; }
	void setMainLoadingInProgress(const bool b) { m_mainLoadingInProgress = b; }

	static const char *getPluginHttpUrl() { return "https://obs-plugin.streamlabs.com"; }
	static void createCefBrowser(std::shared_ptr<BrowserElements> browserElements, const std::string &url, const bool startHidden, const bool keepOnTop);

public:
	CefRefPtr<BrowserApp> m_app = nullptr;

	std::shared_ptr<BrowserElements> m_mainBrowser;
	std::shared_ptr<BrowserElements> m_appstoreBrowser;

public:
	int32_t m_obs64_PIDt = 0;
	bool m_allowHideBrowser = true;
	std::atomic<bool> m_cefInit = false;

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

	static void cleanupCefBrowser_Internal(std::shared_ptr<BrowserElements> browserElements);

	std::wstring getCacheDir() const;

	static void DebugInputThread();
	static void CheckForObsThread();

	bool m_mainPageSuccess = false;
	bool m_mainLoadingInProgress = false;
	bool m_cefCreated = false;

	std::mutex m_mutex;

	std::unique_ptr<QApplication> m_qapp = nullptr;

public:
	// Disallow copying
	SlBrowser(const SlBrowser &) = delete;
	SlBrowser &operator=(const SlBrowser &) = delete;
};
