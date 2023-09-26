#pragma once

#include <thread>
#include <string>

class WebServer
{
public:
	bool start(const int32_t port);
	void stop();
	int32_t getPort() { return m_port; }
	void setExpectedReferer(const std::string &uri) { m_expectedReferer = uri; }
	const std::string& getErr() const { return m_err; }

public:
	static WebServer& instance()
	{
		static WebServer a;
		return a;
	}

private:
	WebServer();
	~WebServer();

	void workerThread();

	int32_t m_port = 0;
	bool m_launching = false;
	bool m_running = false;
	std::thread m_workerThread;
	std::string m_err;
	std::string m_token;

	// Example, http://localhost:25341/?secret_token=
	std::string m_expectedReferer;
};
