#include "QtGuiModifications.h"
#include "GrpcPlugin.h"

// Stl
#include <string>

// Obs
#include <obs.hpp>
#include <obs-frontend-api.h>
#include <obs-module.h>

// Qt
#include <QMainWindow>
#include <QDockWidget>
#include <QCheckBox>
#include <QMessageBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QApplication>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QObject>

#include "QtPostTask.h"

// Windows
#include <Windows.h>

using namespace json11;

QtGuiModifications::QtGuiModifications()
{
	init();
}

QtGuiModifications::~QtGuiModifications()
{

}

void QtGuiModifications::init()
{
	/**
	* GUI Button
	*/

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	QDockWidget *controlsDock = (QDockWidget *)mainWindow->findChild<QDockWidget *>("controlsDock");

	m_obs_streamButton = (QPushButton *)controlsDock->findChild<QPushButton *>("streamButton");

	if (m_obs_streamButton)
		m_obs_streamButton->setVisible(false);

	QVBoxLayout *buttonsVLayout = (QVBoxLayout *)controlsDock->findChild<QVBoxLayout *>("buttonsVLayout");

	if (buttonsVLayout)
	{
		m_sl_streamButton = new QPushButton();
		m_sl_streamButton->setFixedHeight(28);
		buttonsVLayout->insertWidget(0, m_sl_streamButton);

		QObject::connect(m_sl_streamButton, &QPushButton::clicked, []() {
			QtGuiModifications::instance().onStartStreamingRequest();
		});

		copyStylesOfObsButton();
	}

	/**
	* Hotkey redirection
	*/

	constexpr const char *STR_HOTKEY_NAME_START_STREAMING = "OBSBasic.StartStreaming";

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) -> bool {
			if (obs_hotkey_get_registerer_type(key) == OBS_HOTKEY_REGISTERER_FRONTEND)
			{
				if (strcmp(obs_hotkey_get_name(key), STR_HOTKEY_NAME_START_STREAMING) == 0)
				{
					QtGuiModifications::instance().m_streamingHotkeyId = id;
					return false;
				}
			}

			return true;
		},
		nullptr);

	signal_handler_connect(
		obs_get_signal_handler(), "hotkey_register",
		[](void *data, calldata_t *param) {
			auto key = static_cast<obs_hotkey_t *>(calldata_ptr(param, "key"));

			if (obs_hotkey_get_registerer_type(key) == OBS_HOTKEY_REGISTERER_FRONTEND)
			{
				if (strcmp(obs_hotkey_get_name(key), STR_HOTKEY_NAME_START_STREAMING) == 0)
					QtGuiModifications::instance().m_streamingHotkeyId = obs_hotkey_get_id(key);
			}
		},
		nullptr);

	obs_hotkey_set_callback_routing_func(
		[](void *data, obs_hotkey_id id, bool pressed) {
			if (id == QtGuiModifications::instance().m_streamingHotkeyId)
			{
				QtGuiModifications::instance().onStartStreamingRequest();
			}
			else
			{
				QtPostTask(
					[id, pressed]() {
						obs_hotkey_trigger_routed_callback(id, pressed);
				});
			}
		},
		nullptr);

	obs_hotkey_enable_callback_rerouting(true);

	/**
	* Start async thread
	*/

	m_workerThread = std::thread(&QtGuiModifications::workerThread, this);
}

void QtGuiModifications::outsideInvokeClickStreamButton()
{
	m_streamKeyCache.clear();

	obs_service_t *service = obs_frontend_get_streaming_service();
	OBSDataAutoRelease settings = obs_service_get_settings(service);

	if (const char *key = obs_data_get_string(settings, "key"))
		m_streamKeyCache = key;

	QtGuiModifications::instance().m_obs_streamButton->click();
}

void QtGuiModifications::setJavascriptToCallOnStreamClick(const std::string &str)
{
	std::lock_guard<std::recursive_mutex> grd(m_mutex);
	m_jsToCallOnStreamClick = str;
}

/*static*/
void QtGuiModifications::handle_obs_frontend_event(obs_frontend_event event, void *data)
{
	switch (event)
	{
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
	{
		if (!QtGuiModifications::instance().m_streamKeyCache.empty())
		{
			obs_service_t *service = obs_frontend_get_streaming_service();
			OBSDataAutoRelease settings = obs_service_get_settings(service);
			obs_data_set_string(settings, "key", QtGuiModifications::instance().m_streamKeyCache.c_str());
			obs_service_update(service, settings);

			QtGuiModifications::instance().m_streamKeyCache.clear();
		}

		break;
	}
	}
}

void QtGuiModifications::onStartStreamingRequest()
{
	if (!m_jsToCallOnStreamClick.empty())
	{
		// Run thread as to not block
		std::thread([&]() { GrpcPlugin::instance().getClient()->send_executeJavascript(m_jsToCallOnStreamClick); }).detach();
		return;
	}

	QtGuiModifications::instance().m_obs_streamButton->click();
	copyStylesOfObsButton();
}

void QtGuiModifications::copyStylesOfObsButton()
{
	m_sl_streamButton->setText(m_obs_streamButton->text());
	m_sl_streamButton->setEnabled(true);
	m_sl_streamButton->setStyleSheet(m_obs_streamButton->styleSheet());
	m_sl_streamButton->setMenu(m_obs_streamButton->menu());
}

void QtGuiModifications::stop()
{
	m_closing = true;

	if (m_workerThread.joinable())
		m_workerThread.join();
}

void QtGuiModifications::workerThread()
{
	while (!m_closing)
	{
		QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

		// This code is executed in the context of the QMainWindow's thread.
		QMetaObject::invokeMethod(
			mainWindow,
			[mainWindow]() {
				QtGuiModifications::instance().copyStylesOfObsButton();
			},
			Qt::BlockingQueuedConnection);

		// Tick every 100ms, cheap and will look responsive
		::Sleep(100);
	}
}
