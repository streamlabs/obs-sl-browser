#pragma once

#include <mutex>
#include <thread>
#include <vector>
#include <obs.h>

#include <json11/json11.hpp>

class QPushButton;

class QtGuiModifications
{
public:
	static QtGuiModifications &instance()
	{
		static QtGuiModifications a;
		return a;
	}

public:
	void stop();
	void outsideInvokeClickStreamButton();
	void setJavascriptToCallOnStreamClick(const std::string& str);

	static void handle_obs_frontend_event(enum obs_frontend_event event, void *data);

private:
	QtGuiModifications();
	~QtGuiModifications();

	void init();
	void onStartStreamingRequest();
	void copyStylesOfObsButton();
	void workerThread();

	QPushButton *m_obs_streamButton = nullptr;
	QPushButton *m_sl_streamButton = nullptr;

	size_t m_streamingHotkeyId = 0;
	std::string m_jsToCallOnStreamClick = "";
	std::recursive_mutex m_mutex;
	std::thread m_workerThread;
	std::string m_streamKeyCache;

	bool m_closing = false;
};
