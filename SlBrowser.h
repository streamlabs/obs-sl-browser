#pragma once

#include "browser-client.hpp"
#include "browser-app.hpp"

#include <QWidget>

class SlBrowser
{
public:
	void run(int argc, char *argv[]);

	static void CreateCefBrowser(int arg);

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

	static void DebugInputThread();
	static void CheckForObsThread();

public:
	// Disallow copying
	SlBrowser(const SlBrowser &) = delete;
	SlBrowser &operator=(const SlBrowser &) = delete;
};
