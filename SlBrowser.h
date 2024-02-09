#pragma once

#include "browser-client.hpp"
#include "browser-app.hpp"

#include <QWidget>

class SlBrowser
{
public:
	void run(int argc, char *argv[]);

	static void CreateCefBrowser(int arg);
	static const char *getDefaultUrl() { return "https://obs-plugin.streamlabs.com"; }

	bool getSavedHiddenState() const;
	void saveHiddenState(const bool b) const;

	bool getMainPageSuccess() const { return m_mainPageSuccess; }
	bool getMainLoadingInProgress() const { return m_mainLoadingInProgress; }

	void setMainPageSuccess(const bool b) { m_mainPageSuccess = b; }
	void setMainLoadingInProgress(const bool b) { m_mainLoadingInProgress = b; }

public:
	QWidget *m_widget = nullptr;
	CefRefPtr<BrowserApp> m_app = nullptr;
	CefRefPtr<CefBrowser> m_browser = nullptr;
	CefRefPtr<BrowserClient> browserClient = nullptr;
	int32_t m_obs64_PIDt = 0;
	bool m_allowHideBrowser = true;

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

	std::wstring getCacheDir() const;

	static void DebugInputThread();
	static void CheckForObsThread();

	bool m_mainPageSuccess = false;
	bool m_mainLoadingInProgress = false;

public:
	// Disallow copying
	SlBrowser(const SlBrowser &) = delete;
	SlBrowser &operator=(const SlBrowser &) = delete;
};
