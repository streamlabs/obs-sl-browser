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
	void clickStreamButton();
	void setJavascriptToCallOnStreamClick(const std::string& str);

private:
	QtGuiModifications();
	~QtGuiModifications();

	void init();
	void onStartStreamingRequest();
	void copyStylesOfObsButton();

	QPushButton *m_obs_streamButton = nullptr;
	QPushButton *m_sl_streamButton = nullptr;

	size_t m_streamingHotkeyId = 0;
	std::recursive_mutex m_mutex;
	std::string m_jsToCallOnStreamClick = "";
};
