#include "PluginJsHandler.h"

// Local
#include "JavascriptApi.h"
#include "GrpcPlugin.h"
#include "WebServer.h"
#include "Util.h"

// Windows
#include <ShlObj.h>
#include <shellapi.h>

// Stl
#include <chrono>
#include <functional>
#include <codecvt>

// Obs
#include <obs.hpp>
#include <obs-frontend-api.h>

#include "../obs-browser/panel/browser-panel-internal.hpp"

// Qt
#include <QMainWindow>
#include <QDockWidget>
#include <QCheckBox>
#include <QMessageBox>
#include <QComboBox>

using namespace json11;

PluginJsHandler::PluginJsHandler() {}

PluginJsHandler::~PluginJsHandler()
{
	stop();
}

std::wstring PluginJsHandler::getDownloadsDir() const
{
	wchar_t path[MAX_PATH];

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
		return std::wstring(path) + L"\\StreamlabsOBS";

	return L"";
}

void PluginJsHandler::start()
{
	m_running = true;
	m_workerThread = std::thread(&PluginJsHandler::workerThread, this);
}

void PluginJsHandler::stop()
{
	m_running = false;

	if (m_workerThread.joinable())
		m_workerThread.join();
}

void PluginJsHandler::pushApiRequest(const std::string &funcName, const std::string &params)
{
	std::lock_guard<std::mutex> grd(m_queueMtx);
	m_queudRequests.push_back({funcName, params});
}

void PluginJsHandler::workerThread()
{
	while (m_running)
	{
		std::vector<std::pair<std::string, std::string>> latestBatch;

		{
			std::lock_guard<std::mutex> grd(m_queueMtx);
			latestBatch.swap(m_queudRequests);
		}

		if (latestBatch.empty())
		{
			using namespace std::chrono;
			std::this_thread::sleep_for(1ms);
		}
		else
		{
			for (auto &itr : latestBatch)
				executeApiRequest(itr.first, itr.second);
		}
	}
}

void PluginJsHandler::executeApiRequest(const std::string &funcName, const std::string &params)
{
	std::string err;
	Json jsonParams = Json::parse(params, err);

	if (!err.empty())
	{
		blog(LOG_ERROR, "PluginJsHandler::executeApiRequest invalid params %s", params.c_str());
		return;
	}

	const auto &param1Value = jsonParams["param1"];

	if (param1Value.is_null())
	{
		blog(LOG_ERROR, "PluginJsHandler::executeApiRequest Error: 'param1' key not found. %s", params.c_str());
		return;
	}

	blog(LOG_INFO, "executeApiRequest (start) %s: %s\n", funcName.c_str(), params.c_str());

	std::string jsonReturnStr;

	switch (JavascriptApi::getFunctionId(funcName)) {
		case JavascriptApi::JS_QUERY_DOCKS: JS_QUERY_DOCKS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_EXECUTEJAVASCRIPT: JS_DOCK_EXECUTEJAVASCRIPT(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_SETURL: JS_DOCK_SETURL(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOWNLOAD_ZIP: JS_DOWNLOAD_ZIP(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOWNLOAD_FILE: JS_DOWNLOAD_FILE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_READ_FILE: JS_READ_FILE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DELETE_FILES: JS_DELETE_FILES(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DROP_FOLDER: JS_DROP_FOLDER(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_QUERY_DOWNLOADS_FOLDER: JS_QUERY_DOWNLOADS_FOLDER(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_OBS_SOURCE_CREATE: JS_OBS_SOURCE_CREATE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_OBS_SOURCE_DESTROY: JS_OBS_SOURCE_DESTROY(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_SETAREA: JS_DOCK_SETAREA(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_RESIZE: JS_DOCK_RESIZE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_NEW_BROWSER_DOCK: JS_DOCK_NEW_BROWSER_DOCK(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_MAIN_WINDOW_GEOMETRY: JS_GET_MAIN_WINDOW_GEOMETRY(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_TOGGLE_USER_INPUT: JS_TOGGLE_USER_INPUT(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_TOGGLE_DOCK_VISIBILITY: JS_TOGGLE_DOCK_VISIBILITY(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_SWAP: JS_DOCK_SWAP(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DESTROY_DOCK: JS_DESTROY_DOCK(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_RENAME: JS_DOCK_RENAME(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_DOCK_SETTITLE: JS_DOCK_SETTITLE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_STREAMSETTINGS: JS_SET_STREAMSETTINGS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_STREAMSETTINGS: JS_GET_STREAMSETTINGS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_START_WEBSERVER: JS_START_WEBSERVER(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_STOP_WEBSERVER: JS_STOP_WEBSERVER(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_LAUNCH_OS_BROWSER_URL: JS_LAUNCH_OS_BROWSER_URL(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_AUTH_TOKEN: JS_GET_AUTH_TOKEN(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_CLEAR_AUTH_TOKEN: JS_CLEAR_AUTH_TOKEN(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_CURRENT_SCENE: JS_SET_CURRENT_SCENE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_CREATE_SCENE: JS_CREATE_SCENE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SCENE_ADD: JS_SCENE_ADD(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SOURCE_GET_PROPERTIES: JS_SOURCE_GET_PROPERTIES(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SOURCE_GET_SETTINGS: JS_SOURCE_GET_SETTINGS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SOURCE_SET_SETTINGS: JS_SOURCE_SET_SETTINGS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_INSTALL_FONT: JS_INSTALL_FONT(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENE_COLLECTIONS: JS_GET_SCENE_COLLECTIONS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_CURRENT_SCENE_COLLECTION: JS_GET_CURRENT_SCENE_COLLECTION(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_CURRENT_SCENE_COLLECTION: JS_SET_CURRENT_SCENE_COLLECTION(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_ADD_SCENE_COLLECTION: JS_ADD_SCENE_COLLECTION(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCENEITEM_POS: JS_SET_SCENEITEM_POS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCENEITEM_ROT: JS_SET_SCENEITEM_ROT(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCENEITEM_CROP: JS_SET_SCENEITEM_CROP(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCENEITEM_SCALE_FILTER: JS_SET_SCENEITEM_SCALE_FILTER(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCENEITEM_BLENDING_MODE: JS_SET_SCENEITEM_BLENDING_MODE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCENEITEM_BLENDING_METHOD: JS_SET_SCENEITEM_BLENDING_METHOD(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SET_SCALE: JS_SET_SCALE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENEITEM_POS: JS_GET_SCENEITEM_POS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENEITEM_ROT: JS_GET_SCENEITEM_ROT(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENEITEM_CROP: JS_GET_SCENEITEM_CROP(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENEITEM_SCALE_FILTER: JS_GET_SCENEITEM_SCALE_FILTER(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENEITEM_BLENDING_MODE: JS_GET_SCENEITEM_BLENDING_MODE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCENEITEM_BLENDING_METHOD: JS_GET_SCENEITEM_BLENDING_METHOD(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_SCALE: JS_GET_SCALE(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_SCENE_GET_SOURCES: JS_SCENE_GET_SOURCES(jsonParams, jsonReturnStr); break;	
		case JavascriptApi::JS_QUERY_ALL_SOURCES: JS_QUERY_ALL_SOURCES(jsonParams, jsonReturnStr); break;		
		case JavascriptApi::JS_GET_SOURCE_DIMENSIONS: JS_GET_SOURCE_DIMENSIONS(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_GET_CANVAS_DIMENSIONS: JS_GET_CANVAS_DIMENSIONS(jsonParams,jsonReturnStr); break;
		case JavascriptApi::JS_GET_CURRENT_SCENE: JS_GET_CURRENT_SCENE(jsonParams,jsonReturnStr); break;
		case JavascriptApi::JS_OBS_BRING_FRONT: JS_OBS_BRING_FRONT(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_OBS_TOGGLE_HIDE_SELF: JS_OBS_TOGGLE_HIDE_SELF(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_OBS_ADD_TRANSITION: JS_OBS_ADD_TRANSITION(jsonParams, jsonReturnStr); break;
		case JavascriptApi::JS_OBS_SET_CURRENT_TRANSITION: JS_OBS_SET_CURRENT_TRANSITION(jsonParams, jsonReturnStr); break;
		default: jsonReturnStr = Json(Json::object{{"error", "Unknown Javascript Function"}}).dump(); break;
	}
		
	blog(LOG_INFO, "executeApiRequest (finish) %s: %s\n", funcName.c_str(), jsonReturnStr.c_str());

	// We're done, send callback
	if (param1Value.int_value() > 0)
		GrpcPlugin::instance().getClient()->send_executeCallback(param1Value.int_value(), jsonReturnStr);
}

void PluginJsHandler::JS_START_WEBSERVER(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	WebServer::instance().setExpectedReferer(param3Value.string_value());
	WebServer::instance().setRedirectUrl(param4Value.string_value());

	if (!WebServer::instance().start(param2Value.int_value()))
	{
		out_jsonReturn = Json(Json::object{{"error", WebServer::instance().getErr()}}).dump();
		return;
	}

	out_jsonReturn = Json(Json::object{{"port", WebServer::instance().getPort()}}).dump();
}

void PluginJsHandler::JS_STOP_WEBSERVER(const json11::Json &params, std::string &out_jsonReturn)
{
	WebServer::instance().stop();
}

void PluginJsHandler::JS_LAUNCH_OS_BROWSER_URL(const json11::Json &params, std::string &out_jsonReturn)
{	
	std::map<INT_PTR, std::string> shellExecuteErrors = {
		{0, "The operating system is out of memory or resources."},
		{ERROR_FILE_NOT_FOUND, "The specified file was not found."},
		{ERROR_PATH_NOT_FOUND, "The specified path was not found."},
		{ERROR_BAD_FORMAT, "The .exe file is invalid (non-Win32 .exe or error in .exe image)."},
		{SE_ERR_ACCESSDENIED, "The operating system denied access to the specified file."},
		{SE_ERR_ASSOCINCOMPLETE, "The file name association is incomplete or invalid."},
		{SE_ERR_DDEBUSY, "The DDE transaction could not be completed because other DDE transactions were being processed."},
		{SE_ERR_DDEFAIL, "The DDE transaction failed."},
		{SE_ERR_DDETIMEOUT, "The DDE transaction could not be completed because the request timed out."},
		{SE_ERR_DLLNOTFOUND, "The specified DLL was not found."},
		{SE_ERR_FNF, "The specified file was not found."},
		{SE_ERR_NOASSOC, "There is no application associated with the given file name extension. This error will also be returned if you attempt to print a file that is not printable."},
		{SE_ERR_OOM, "There was not enough memory to complete the operation."},
		{SE_ERR_PNF, "The specified path was not found."},
		{SE_ERR_SHARE, "A sharing violation occurred."},
	};

	const auto &param2Value = params["param2"];
	std::string url = param2Value.string_value();
	INT_PTR result = (INT_PTR)::ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);

	// ShellExecuteA
	// "If the function succeeds, it returns a value greater than 32."
	if (result <= 32)
	{
		std::string err = "An unknown error occured. GetLastError() = " + GetLastError();
		auto itr = shellExecuteErrors.find(result);

		if (itr != shellExecuteErrors.end())
			err = itr->second;

		out_jsonReturn = Json(Json::object{{"error", err}}).dump();
	}
	else
	{
		out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
	}
}

void PluginJsHandler::JS_GET_AUTH_TOKEN(const json11::Json &params, std::string &out_jsonReturn)
{
	out_jsonReturn = Json(Json::object{{"token", WebServer::instance().getToken()}}).dump();
}

void PluginJsHandler::JS_CLEAR_AUTH_TOKEN(const json11::Json &params, std::string &out_jsonReturn)
{
	WebServer::instance().clearToken();
	out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
}

void PluginJsHandler::JS_GET_STREAMSETTINGS(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			obs_service_t *service_t = obs_frontend_get_streaming_service();

			if (service_t)
			{
				obs_data_t *settings = obs_service_get_settings(service_t);

				std::string service = obs_data_get_string(settings, "service");
				std::string protocol = obs_data_get_string(settings, "protocol");
				std::string server = obs_data_get_string(settings, "server");
				bool use_auth = obs_data_get_bool(settings, "use_auth");
				std::string username = obs_data_get_string(settings, "username");
				std::string password = obs_data_get_string(settings, "password");
				std::string key = obs_data_get_string(settings, "key");

				out_jsonReturn = Json(Json::object{{"service", service}, {"protocol", protocol}, {"server", server}, {"use_auth", use_auth}, {"username", username}, {"password", password}, {"key", key}}).dump();
			}
			else
			{
				out_jsonReturn = Json(Json::object({{"error", "No service exists"}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_STREAMSETTINGS(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];
	const auto &param5Value = params["param5"];
	const auto &param6Value = params["param6"];
	const auto &param7Value = params["param7"];
	const auto &param8Value = params["param8"];

	// "rtmp_custom" : "rtmp_common"
	std::string service = param2Value.string_value();
	std::string protocol = param3Value.string_value();
	std::string server = param4Value.string_value();
	bool use_auth = param5Value.bool_value();
	std::string username = param6Value.string_value();
	std::string password = param7Value.string_value();
	std::string key = param8Value.string_value();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &service, &protocol, &server, use_auth, &username, &password, &key, &out_jsonReturn]() {

			obs_service_t *oldService = obs_frontend_get_streaming_service();
			OBSDataAutoRelease hotkeyData = obs_hotkeys_save_service(oldService);

			OBSDataAutoRelease settings = obs_data_create();

			obs_data_set_string(settings, "service", service.c_str());
			obs_data_set_string(settings, "protocol", protocol.c_str());
			obs_data_set_string(settings, "server", server.c_str());
			obs_data_set_bool(settings, "use_auth", use_auth);
			obs_data_set_string(settings, "username", username.c_str());
			obs_data_set_string(settings, "password", password.c_str());
			obs_data_set_string(settings, "key", key.c_str());

			OBSServiceAutoRelease newService = obs_service_create(service.c_str(), "default_service", settings, hotkeyData);

			if (!newService)
				return;

			obs_frontend_set_streaming_service(newService);
			obs_frontend_save_streaming_service();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_QUERY_DOCKS(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			std::vector<Json> dockInfo;

			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				bool isSlabs = false;
				std::string name = dock->objectName().toStdString();
				std::string url;

				// Translate the global coordinates to coordinates relative to the main window
				QRect globalGeometry = dock->geometry();
				QRect mainWindowGeometry = mainWindow->geometry();
				int x = globalGeometry.x() - mainWindowGeometry.x();
				int y = globalGeometry.y() - mainWindowGeometry.y();
				int width = dock->width();
				int height = dock->height();
				bool floating = dock->isFloating();
				bool visible = dock->isVisible();
				std::string dockName = dock->objectName().toStdString();
				std::string dockTitle = dock->windowTitle().toStdString();

				if (dock->property("isSlabs").isValid())
				{
					isSlabs = true;
					QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();

					if (widget->cefBrowser != nullptr)
						url = widget->cefBrowser->GetMainFrame()->GetURL();
				}

				// Create a Json object for this dock widget and add it to the panelInfo vector
				dockInfo.push_back(Json::object{{"name", name}, {"x", x}, {"y", y}, {"width", width}, {"height", height}, {"floating", floating},
					{"isSlabs", isSlabs}, {"url", url}, {"visible", visible}, {"title", dockTitle}});
			}

			// Convert the panelInfo vector to a Json object and dump string
			Json ret = dockInfo;
			out_jsonReturn = ret.dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SWAP(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName1 = param2Value.string_value();
	std::string objectName2 = param3Value.string_value();

	// Assume failure until we find the docks and swap them
	out_jsonReturn = Json(Json::object({{"error", "Did not find docks with objectNames: " + objectName1 + " and " + objectName2}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName1, objectName2, &out_jsonReturn]() {
			QDockWidget *dock1 = nullptr;
			QDockWidget *dock2 = nullptr;

			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName1)
					dock1 = dock;
				else if (dock->objectName().toStdString() == objectName2)
					dock2 = dock;

				if (dock1 && dock2)
					break;
			}

			if (dock1 && dock2)
			{
				QRect geo1 = dock1->geometry();
				QRect geo2 = dock2->geometry();

				// Swap the geometries
				dock1->setGeometry(geo2);
				dock2->setGeometry(geo1);

				out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_RESIZE(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string objectName = param2Value.string_value();
	int width = param3Value.int_value();
	int height = param3Value.int_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, width, height, &out_jsonReturn]() {
			// Find the panel by name (assuming the name is stored as a property)
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					dock->resize(width, height);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SETAREA(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	int areaMask = param3Value.int_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, areaMask, &out_jsonReturn]() {
			// Find the panel by name (assuming the name is stored as a property)
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					if (dock->isFloating())
						dock->setFloating(false);

					// Map the input area mask to the corresponding Qt::DockWidgetArea
					Qt::DockWidgetArea dockArea = static_cast<Qt::DockWidgetArea>(areaMask & Qt::DockWidgetArea_Mask);
					mainWindow->addDockWidget(dockArea, dock);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_EXECUTEJAVASCRIPT(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string javascriptcode = param3Value.string_value();

	if (javascriptcode.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, javascriptcode, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName && dock->property("isSlabs").isValid())
				{
					QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();

					if (widget->cefBrowser != nullptr)
					{
						widget->cefBrowser->GetMainFrame()->ExecuteJavaScript(javascriptcode.c_str(), widget->cefBrowser->GetMainFrame()->GetURL(), 0);
						out_jsonReturn = Json(Json::object{{"status", "Found dock and ran ExecuteJavaScript on " + widget->cefBrowser->GetMainFrame()->GetURL().ToString()}}).dump();
					}

					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_TOGGLE_USER_INPUT(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	bool enable = params["param2"].bool_value();

	QMetaObject::invokeMethod(
		mainWindow, [mainWindow, enable]() { ::EnableWindow(reinterpret_cast<HWND>(mainWindow->winId()), enable); }, Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_NEW_BROWSER_DOCK(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string title = param2Value.string_value();
	std::string url = param3Value.string_value();
	std::string objectName = param4Value.string_value();

	if (objectName.empty() || title.empty() || url.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, title, url, &out_jsonReturn]() {
			// Check duplication
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					out_jsonReturn = Json(Json::object({{"error", "Already exists"}})).dump();
					return;
				}
			}

			static QCef *qcef = obs_browser_init_panel();

			QDockWidget *dock = new QDockWidget(mainWindow);
			QCefWidget *browser = qcef->create_widget(dock, url, nullptr);
			dock->setWidget(browser);
			dock->setWindowTitle(title.c_str());
			dock->setObjectName(objectName.c_str());
			dock->setProperty("isSlabs", true);

			// obs_frontend_add_dock and keep the pointer to it
			dock->setProperty("actionptr", (uint64_t)obs_frontend_add_dock(dock));

			dock->resize(460, 600);
			dock->setMinimumSize(80, 80);
			dock->setWindowTitle(title.c_str());
			dock->setAllowedAreas(Qt::AllDockWidgetAreas);
			dock->setWidget(browser);

			mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);

			// Can't use yet
			//obs_frontend_add_dock_by_id(objectName.c_str(), title.c_str(), nullptr);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_MAIN_WINDOW_GEOMETRY(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			int x = mainWindow->geometry().x();
			int y = mainWindow->geometry().y();
			int width = mainWindow->width();
			int height = mainWindow->height();
			out_jsonReturn = Json(Json::object{{{"x", x}, {"y", y}, {"width", width}, {"height", height}}}).dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SETURL(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string url = param3Value.string_value();

	if (url.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, url, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName && dock->property("isSlabs").isValid())
				{
					QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();
					widget->setURL(url.c_str());
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_TOGGLE_DOCK_VISIBILITY(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	bool visible = param3Value.bool_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, visible, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					dock->setVisible(visible);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SETTITLE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string newTitle = param3Value.string_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, newTitle, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					QAction *action = reinterpret_cast<QAction *>(dock->property("actionptr").toULongLong());
					action->setText(newTitle.c_str());
					dock->setWindowTitle(newTitle.c_str());
					out_jsonReturn = Json(Json::object({{"error", "success"}})).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_RENAME(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string newName = param3Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, newName, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();

			// Check for match against the new name
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == newName)
				{
					out_jsonReturn = Json(Json::object({{"error", "Dock with that already exists: " + objectName}})).dump();
					return;
				}
			}

			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					dock->setObjectName(newName);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					return;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DESTROY_DOCK(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string objectName = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, &out_jsonReturn]() {
			//obs_frontend_remove_dock(objectName.c_str());
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SOURCE_GET_SETTINGS(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string sourceName = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, sourceName, &out_jsonReturn]() {
			OBSSourceAutoRelease existingSource = obs_get_source_by_name(sourceName.c_str());
			if (existingSource == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + sourceName}})).dump();
				return;
			}

			obs_data_t *settingsSource = obs_source_get_settings(existingSource);
			if (settingsSource == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Error getting settings from " + sourceName}})).dump();
				return;
			}

			out_jsonReturn = Json(obs_data_get_json(settingsSource)).dump();
			obs_data_release(settingsSource);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SOURCE_SET_SETTINGS(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	std::string sourceName = param2Value.string_value();
	std::string settingsJson = param3Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, sourceName, settingsJson, &out_jsonReturn]() {
			OBSSourceAutoRelease existingSource = obs_get_source_by_name(sourceName.c_str());
			if (existingSource == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + sourceName}})).dump();
				return;
			}

			obs_data_t *newSettings = obs_data_create_from_json(settingsJson.c_str());
			if (newSettings == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Error parsing settings JSON"}})).dump();
				return;
			}

			obs_source_update(existingSource, newSettings);
			obs_data_release(newSettings);

			out_jsonReturn = Json(Json::object({{"success", true}})).dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_OBS_SET_CURRENT_TRANSITION(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string sourceName = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, sourceName, &out_jsonReturn]() {

			obs_frontend_source_list transitions = {};
			obs_frontend_get_transitions(&transitions);

			OBSSourceAutoRelease transition;
			for (size_t i = 0; i < transitions.sources.num; i++)
			{
				obs_source_t *source = transitions.sources.array[i];
				if (obs_source_get_name(source) == sourceName)
				{
					transition = obs_source_get_ref(source);
					break;
				}
			}

			obs_frontend_source_list_free(&transitions);

			if (!transition)
			{
				out_jsonReturn = Json(Json::object({{"error", "Did not find transition named " + sourceName}})).dump();
				return;
			}

			obs_frontend_set_current_transition(transition);
		},
		Qt::BlockingQueuedConnection);	
}

void PluginJsHandler::JS_OBS_ADD_TRANSITION(const json11::Json& params, std::string& out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string sourceName = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, sourceName, & out_jsonReturn]() {

			obs_frontend_source_list transitions = {};
			obs_frontend_get_transitions(&transitions);

			OBSSourceAutoRelease transition;
			for (size_t i = 0; i < transitions.sources.num; i++)
			{
				obs_source_t *source = transitions.sources.array[i];
				if (obs_source_get_name(source) == sourceName)
				{
					transition = obs_source_get_ref(source);
					break;
				}
			}

			obs_frontend_source_list_free(&transitions);

			if (transition != nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Transition already exists named " + sourceName}})).dump();
				return;
			}

			// TODO: OBS needs frontend support for this, I'm looking for their transitions widget and manipulating it here with duplicated code from the UI
			QList<QWidget *> allWidgets = mainWindow->findChildren<QWidget *>();
			foreach(QWidget * widget, allWidgets)
			{
				if (widget->objectName().toStdString() == "transitions")
				{
					QComboBox *transitions = (QComboBox *)widget;
					obs_source_t *source = obs_source_create_private("swipe_transition", sourceName.c_str(), NULL);

					if (!source)
					{
						out_jsonReturn = Json(Json::object({{"error", "Failed to create the object"}})).dump();	
						return;
					}

					auto onTransitionStop = [](void *data, calldata_t *) {
						QMainWindow *window = (QMainWindow *)data;
						QMetaObject::invokeMethod(window, "TransitionStopped", Qt::QueuedConnection);
					};

					auto onTransitionFullStop = [](void *data, calldata_t *) {
						QMainWindow *window = (QMainWindow *)data;
						QMetaObject::invokeMethod(window, "TransitionFullyStopped", Qt::QueuedConnection);
					};

					signal_handler_t *handler = obs_source_get_signal_handler(source);
					signal_handler_connect(handler, "transition_video_stop", onTransitionStop, mainWindow);
					signal_handler_connect(handler, "transition_stop", onTransitionFullStop, mainWindow);

					transitions->addItem(sourceName.c_str(), QVariant::fromValue(OBSSource(source)));
					//transitions->setCurrentIndex(transitions->count() - 1);

					obs_source_release(source);

					out_jsonReturn = Json(Json::object({{"success", true}})).dump();
					return;
				}
			}

			out_jsonReturn = Json(Json::object({{"error", "Unable to find transitions widget"}})).dump();	
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_OBS_TOGGLE_HIDE_SELF(const json11::Json& params, std::string& out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	bool boolval = param2Value.bool_value();
	
	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, boolval, &out_jsonReturn]() {
			mainWindow->setHidden(boolval);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_OBS_BRING_FRONT(const json11::Json& params, std::string& out_jsonReturn)
{
	DWORD currentProcessId = ::GetCurrentProcessId();

	EnumWindows(
		[](HWND hWnd, LPARAM lParam) -> BOOL {
			DWORD processId;
			::GetWindowThreadProcessId(hWnd, &processId);

			if (processId == (DWORD)lParam)
				Util::ForceForegroundWindow(hWnd);
			
			return TRUE;
		},
		(LPARAM)currentProcessId);
}

void PluginJsHandler::JS_GET_CURRENT_SCENE(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			OBSSourceAutoRelease current_scene_source = obs_frontend_get_current_scene();
			out_jsonReturn = Json(Json::object({{"name", obs_source_get_name(current_scene_source)}})).dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_CURRENT_SCENE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string scene_name = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, scene_name, &out_jsonReturn]() {
			OBSSourceAutoRelease source = obs_get_source_by_name(scene_name.c_str());
			if (!source)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();			
			else if (!obs_source_is_scene(source))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();			
			else
				obs_frontend_set_current_scene(source);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SCENE_ADD(const json11::Json& params, std::string& out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			OBSSourceAutoRelease source = obs_get_source_by_name(source_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!source)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + source_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);

				if (obs_scene_find_source(scene_obj, source_name.c_str()))
				{
					out_jsonReturn = Json(Json::object({{"error", "The source is already in the scene"}})).dump();
					return;
				}

				obs_sceneitem_t *scene_item = obs_scene_add(scene_obj, source);
				if (!scene_item)
					out_jsonReturn = Json(Json::object({{"error", "Failed to add source to scene"}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SOURCE_GET_PROPERTIES(const json11::Json& params, std::string& out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string source_name = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease existingSource = obs_get_source_by_name(source_name.c_str());

			if (existingSource == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Source not found: " + source_name}})).dump();
				return;
			}

			obs_properties_t *prp = obs_source_properties(existingSource);
			obs_data_t *settings = obs_source_get_settings(existingSource);

			Json::array jsonProperties;

			const char *buf = nullptr;
			for (obs_property_t *p = obs_properties_first(prp); (p != nullptr); obs_property_next(&p))
			{
				Json::object propJson;
				const char *name = obs_property_name(p);

				switch (obs_property_get_type(p))
				{
				case OBS_PROPERTY_BOOL:
				{
					propJson = {{name, obs_data_get_bool(settings, name)}};
					break;
				}
				case OBS_PROPERTY_INT:
				{
					Json::object valueObj;
					valueObj["type"] = "integer";
					valueObj["value"] = (int)obs_data_get_int(settings, name);
					valueObj["min"] = obs_property_int_min(p);
					valueObj["max"] = obs_property_int_max(p);
					valueObj["step"] = obs_property_int_step(p);
					propJson[name] = valueObj;
					break;
				}
				case OBS_PROPERTY_FLOAT:
				{
					Json::object valueObj = {{"type", "float"}, {"value", obs_data_get_double(settings, name)}, {"min", obs_property_float_min(p)}, {"max", obs_property_float_max(p)}, {"step", obs_property_float_step(p)}};
					propJson[name] = valueObj;
					break;
				}
				case OBS_PROPERTY_TEXT:
				{
					const char *valueBuf = obs_data_get_string(settings, name);
					std::string value = valueBuf ? valueBuf : "";
					Json::object valueObj = {{"type", "text"}, {"value", value}};
					propJson[name] = valueObj;
					break;
				}
				case OBS_PROPERTY_PATH: {
					const char *valueBuf = obs_data_get_string(settings, name);
					const char *filterBuf = obs_property_path_filter(p);
					const char *defaultPathBuf = obs_property_path_default_path(p);

					Json::object valueObj = {{"type", "path"}, {"value", valueBuf ? valueBuf : ""}, {"filter", filterBuf ? filterBuf : ""}, {"default_path", defaultPathBuf ? defaultPathBuf : ""}};
					propJson[name] = valueObj;
					break;
				}
				case OBS_PROPERTY_LIST:
				{
					enum ListType
					{
						InvalidListType,
						Editable,
						List,
					};

					enum Format
					{
						InvalidFormat,
						Integer,
						Float,
						String,
					};

					Json::array itemsArray;
					size_t items = obs_property_list_item_count(p);

					ListType fieldType = ListType(obs_property_list_type(p));
					Format format = Format(obs_property_list_format(p));

					for (size_t idx = 0; idx < items; ++idx)
					{
						const char *itemBuf = obs_property_list_item_name(p, idx);
						Json::object entry = {{"name", itemBuf ? itemBuf : ""}, {"enabled", !obs_property_list_item_disabled(p, idx)}};

						switch (format)
						{
						case Format::Integer:
							entry["value_int"] = (int)obs_property_list_item_int(p, idx);
							break;
						case Format::Float:
							entry["value_float"] = obs_property_list_item_float(p, idx);
							break;
						case Format::String:
							entry["value_string"] = (itemBuf = obs_property_list_item_string(p, idx)) ? itemBuf : "";
							break;
						}
						itemsArray.push_back(entry);
					}

					Json::object listObj = {{"type", "list"},
								{"field_type", static_cast<int>(fieldType)}, // Assuming you want to store the enum value
								{"format", static_cast<int>(format)},
								{"items", itemsArray}};

					propJson[name] = listObj;
					break;
				}

				case OBS_PROPERTY_COLOR_ALPHA:
				case OBS_PROPERTY_COLOR:
				{
					propJson["type"] = "ColorProperty";
					propJson["field_type"] = (int)obs_property_int_type(p);
					propJson["value"] = (int)obs_data_get_int(settings, name);
					break;
				}
				case OBS_PROPERTY_BUTTON:
					propJson["type"] = "ButtonProperty";
					break;
				case OBS_PROPERTY_FONT:
				{
					obs_data_t *font_obj = obs_data_get_obj(settings, name);
					propJson["type"] = "FontProperty";
					propJson["face"] = (buf = obs_data_get_string(font_obj, "face")) != nullptr ? buf : "";
					propJson["style"] = (buf = obs_data_get_string(font_obj, "style")) != nullptr ? buf : "";
					propJson["path"] = (buf = obs_data_get_string(font_obj, "path")) != nullptr ? buf : "";
					propJson["size"] = (int)obs_data_get_int(font_obj, "size");
					propJson["flags"] = (int)obs_data_get_int(font_obj, "flags");
					break;
				}
				case OBS_PROPERTY_EDITABLE_LIST:
				{
					propJson["type"] = "EditableListProperty";
					propJson["field_type"] = int(obs_property_editable_list_type(p));
					propJson["filter"] = (buf = obs_property_editable_list_filter(p)) != nullptr ? buf : "";
					propJson["default_path"] = (buf = obs_property_editable_list_default_path(p)) != nullptr ? buf : "";

					obs_data_array_t *array = obs_data_get_array(settings, name);
					size_t count = obs_data_array_count(array);
					json11::Json::array valuesArray;

					for (size_t idx = 0; idx < count; ++idx)
					{
						obs_data_t *item = obs_data_array_item(array, idx);
						valuesArray.push_back((buf = obs_data_get_string(item, "value")) != nullptr ? buf : "");
					}
					propJson["values"] = valuesArray;
					break;
				}

				case OBS_PROPERTY_FRAME_RATE:
				{
					propJson["type"] = "FrameRateProperty";
					size_t num_ranges = obs_property_frame_rate_fps_ranges_count(p);
					json11::Json::array rangesArray;

					for (size_t idx = 0; idx < num_ranges; idx++)
					{
						auto min = obs_property_frame_rate_fps_range_min(p, idx);
						auto max = obs_property_frame_rate_fps_range_max(p, idx);

						json11::Json::object minObj;
						minObj["numerator"] = (int)min.numerator;
						minObj["denominator"] = (int)min.denominator;

						json11::Json::object maxObj;
						maxObj["numerator"] = (int)max.numerator;
						maxObj["denominator"] = (int)max.denominator;

						json11::Json::object rangeObj;
						rangeObj["minimum"] = minObj;
						rangeObj["maximum"] = maxObj;

						rangesArray.push_back(rangeObj);
					}

					propJson["ranges"] = rangesArray;

					size_t num_options = obs_property_frame_rate_options_count(p);
					json11::Json::array optionsArray;

					for (size_t idx = 0; idx < num_options; idx++)
					{
						auto min = obs_property_frame_rate_fps_range_min(p, idx), max = obs_property_frame_rate_fps_range_max(p, idx);
						json11::Json::object optionObj;

						optionObj["name"] = (buf = obs_property_frame_rate_option_name(p, idx)) != nullptr ? buf : "";
						optionObj["description"] = (buf = obs_property_frame_rate_option_description(p, idx)) != nullptr ? buf : "";

						optionsArray.push_back(optionObj);
					}
					propJson["options"] = optionsArray;

					media_frames_per_second fps = {};
					if (obs_data_get_frames_per_second(settings, name, &fps, nullptr))
						propJson["current_fps"] = json11::Json::object{{"numerator", (int)fps.numerator}, {"denominator", (int)fps.denominator}};

					break;
				}
				}

				jsonProperties.push_back(propJson);
			}

			Json output = jsonProperties;
			out_jsonReturn = output.dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_CREATE_SCENE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string scene_name = param2Value.string_value();

	if (scene_name.empty() || scene_name.size() > 1024)
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid scene name " + scene_name}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, scene_name, &out_jsonReturn]() {
			OBSSceneAutoRelease scene = obs_scene_create(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Failed to create scene."}})).dump();
						
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOWNLOAD_ZIP(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string url = param2Value.string_value();
	std::wstring folderPath = getDownloadsDir();

	if (!folderPath.empty())
	{
		using namespace std::chrono;

		// Current time in miliseconds
		system_clock::time_point now = system_clock::now();
		auto duration = now.time_since_epoch();
		auto millis = duration_cast<milliseconds>(duration).count();
		std::wstring millis_str = std::to_wstring(millis);

		// ThreadID + MsTime should be unique, same threaID within 1ms window is a statistical improbability
		std::wstring subFolderPath = folderPath + L"\\" + std::to_wstring(GetCurrentThreadId()) + millis_str;
		std::wstring zipFilepath = subFolderPath + L"\\download.zip";

		CreateDirectoryW(folderPath.c_str(), NULL);
		CreateDirectoryW(subFolderPath.c_str(), NULL);

		auto wstring_to_utf8 = [](const std::wstring &str) {
			std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
			return myconv.to_bytes(str);
		};

		if (Util::DownloadFile(url, wstring_to_utf8(zipFilepath)))
		{
			std::vector<std::string> filepaths;

			if (Util::Unzip(wstring_to_utf8(zipFilepath), filepaths))
			{
				// Build json string now
				Json::array json_array;

				for (const auto &filepath : filepaths)
				{
					Json::object obj;
					obj["path"] = filepath;
					json_array.push_back(obj);
				}

				out_jsonReturn = Json(json_array).dump();
			}
			else
			{
				out_jsonReturn = Json(Json::object({{"error", "Unzip file failed"}})).dump();
			}
		}
		else
		{
			out_jsonReturn = Json(Json::object({{"error", "Http download file failed"}})).dump();
		}

		// zip file itself not needed
		::_wremove(zipFilepath.c_str());
	}
	else
	{
		out_jsonReturn = Json(Json::object({{"error", "File system can't access Local AppData folder"}})).dump();
	}
}

void PluginJsHandler::JS_DOWNLOAD_FILE(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string url = param2Value.string_value();
	std::string filename = param3Value.string_value();
	std::wstring folderPath = getDownloadsDir();

	if (filename.empty() || url.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	auto wstring_to_utf8 = [](const std::wstring &str) {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		return myconv.to_bytes(str);
	};

	auto utf8_to_wstring = [](const std::string &str) {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
		return myconv.from_bytes(str);
	};

	if (!folderPath.empty())
	{
		using namespace std::chrono;

		// Current time in miliseconds
		system_clock::time_point now = system_clock::now();
		auto duration = now.time_since_epoch();
		auto millis = duration_cast<milliseconds>(duration).count();
		std::wstring millis_str = std::to_wstring(millis);

		// ThreadID + MsTime should be unique, same threaID within 1ms window is a statistical improbability
		std::wstring subFolderPath = folderPath + L"\\" + std::to_wstring(GetCurrentThreadId()) + millis_str;
		std::wstring downloadPath = subFolderPath + L"\\" + utf8_to_wstring(filename);

		CreateDirectoryW(folderPath.c_str(), NULL);
		CreateDirectoryW(subFolderPath.c_str(), NULL);

		if (Util::DownloadFile(url, wstring_to_utf8(downloadPath)))
			out_jsonReturn = Json(Json::object({{"path", downloadPath}})).dump();
		else
			out_jsonReturn = Json(Json::object({{"error", "Http download file failed"}})).dump();

		// zip file itself not needed
		::_wremove(downloadPath.c_str());
	}
	else
	{
		out_jsonReturn = Json(Json::object({{"error", "File system can't access Local AppData folder"}})).dump();
	}
}

void PluginJsHandler::JS_INSTALL_FONT(const json11::Json& params, std::string& out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string filepath = param2Value.string_value();

	if (filepath.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid param"}})).dump();
		return;
	}

	if (Util::InstallFont(filepath.c_str()))
		out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
	else
		out_jsonReturn = Json(Json::object({{"error", "WinApi AddFontResourceA failed"}})).dump();
}

void PluginJsHandler::JS_READ_FILE(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];

	std::string filepath = param2Value.string_value();
	std::string filecontents;

	std::ifstream file(filepath, std::ios::binary | std::ios::ate);

	Json ret;

	if (file)
	{
		try
		{
			// Get the file size
			std::streamsize fileSize = file.tellg();
			file.seekg(0, std::ios::beg);

			// Check if file size is 1MB or higher
			if (fileSize >= 1048576)
			{
				ret = Json::object({{"error", "File size is 1MB or higher"}});
			}
			else
			{
				std::stringstream buffer;
				buffer << file.rdbuf();
				filecontents = buffer.str();
				ret = Json::object({{"contents", filecontents}});
			}
		}
		catch (...)
		{
			ret = Json::object({{"error", "Unable to read file. Checking for windows errors: '" + std::to_string(GetLastError()) + "'"}});
		}
	}
	else
	{
		ret = Json::object({{"error", "Unable to open file. Checking for windows errors: '" + std::to_string(GetLastError()) + "'"}});
	}

	out_jsonReturn = ret.dump();
}

void PluginJsHandler::JS_DELETE_FILES(const Json &params, std::string &out_jsonReturn)
{
	Json ret;
	std::vector<std::string> errors;
	std::vector<std::string> success;

	const auto &param2Value = params["param2"];

	std::string err;
	Json jsonArray = Json::parse(params["param2"].string_value().c_str(), err);

	if (!err.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid parameter: " + err}})).dump();
		return;
	}

	const auto &filepaths = jsonArray.array_items();

	for (const auto &filepathJson : filepaths)
	{
		if (filepathJson.is_object())
		{
			const auto &filepath = filepathJson["path"].string_value();

			std::filesystem::path downloadsDir = std::filesystem::path(getDownloadsDir());
			std::filesystem::path fullPath = downloadsDir / filepath;
			std::filesystem::path normalizedPath = fullPath.lexically_normal();

			// Check if filepath contains relative components that move outside the downloads directory
			if (normalizedPath.string().find(downloadsDir.string()) != 0)
			{
				errors.push_back("Invalid path: " + filepath);
			}
			else
			{
				try
				{
					if (std::filesystem::exists(normalizedPath))
					{
						std::filesystem::remove(normalizedPath);
						success.push_back(filepath);
					}
					else
					{
						errors.push_back("File not found: " + filepath);
					}
				}
				catch (const std::filesystem::filesystem_error &e)
				{
					errors.push_back("Error deleting file '" + filepath + "': " + e.what());
				}
			}
		}
	}

	out_jsonReturn = Json(Json::object({{"success", success}, {"errors", errors}})).dump();
}

void PluginJsHandler::JS_DROP_FOLDER(const Json &params, std::string &out_jsonReturn)
{
	const auto &filepath = params["param2"].string_value();

	std::filesystem::path downloadsDir = std::filesystem::path(getDownloadsDir());
	std::filesystem::path fullPath = downloadsDir / filepath;
	std::filesystem::path normalizedPath = fullPath.lexically_normal();

	// Check if filepath contains relative components that move outside the downloads directory
	if (normalizedPath.string().find(downloadsDir.string()) != 0)
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid path: " + filepath}})).dump();
	}
	else
	{
		try
		{
			std::filesystem::remove_all(normalizedPath);
		}
		catch (const std::filesystem::filesystem_error &e)
		{
			out_jsonReturn = Json(Json::object({{"error", "Failed to delete '" + filepath + "': " + e.what()}})).dump();
		}
	}
}

void PluginJsHandler::JS_QUERY_DOWNLOADS_FOLDER(const Json &params, std::string &out_jsonReturn)
{
	std::wstring downloadsFolderFullPath = getDownloadsDir();
	std::vector<Json> pathsList;

	try
	{
		for (const auto &entry : std::filesystem::directory_iterator(downloadsFolderFullPath))
			pathsList.push_back(entry.path().string());

		out_jsonReturn = Json(Json::array(pathsList)).dump();
	}
	catch (const std::filesystem::filesystem_error &e)
	{
		out_jsonReturn = Json(Json::object({{"error", "Failed to query downloads folder: " + std::string(e.what())}})).dump();
	}
}

void PluginJsHandler::JS_OBS_SOURCE_CREATE(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, this, &params, &out_jsonReturn]() {
			const auto &id = params["param2"].string_value();
			const auto &name = params["param3"].string_value();
			const auto &settings_jsonStr = params["param4"].string_value();
			const auto &hotkey_data_jsonStr = params["param5"].string_value();

			// Name is also the guid, duplicates can't exist
			//	see "bool AddNew(QWidget *parent, const char *id, const char *name," in obs gui code
			OBSSourceAutoRelease existingSource = obs_get_source_by_name(name.c_str());

			if (existingSource != nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "name already exists, " + name}})).dump();
				return;
			}

			obs_data_t *settings = obs_data_create_from_json(settings_jsonStr.c_str());
			obs_data_t *hotkeys = obs_data_create_from_json(hotkey_data_jsonStr.c_str());

			obs_source_t *source = obs_source_create(id.c_str(), name.c_str(), settings, hotkeys);

			obs_data_release(hotkeys);
			obs_data_release(settings);

			if (!source)
			{
				out_jsonReturn = Json(Json::object({{"error", "obs_source_create returned null"}})).dump();
				return;
			}

			obs_data_t *settingsSource = obs_source_get_settings(source);

			Json jsonReturnValue = Json::object({{"settings", Json(obs_data_get_json(settingsSource))},
							     {"audio_mixers", Json(std::to_string(obs_source_get_audio_mixers(source)))},
							     {"deinterlace_mode", Json(std::to_string(obs_source_get_deinterlace_mode(source)))},
							     {"deinterlace_field_order", Json(std::to_string(obs_source_get_deinterlace_field_order(source)))}});

			out_jsonReturn = jsonReturnValue.dump();
			obs_data_release(settingsSource);
						
			OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
			obs_scene_t *scene_obj = obs_scene_from_source(scene);

			if (obs_scene_find_source(scene_obj, name.c_str()))
			{
				out_jsonReturn = Json(Json::object({{"error", "The source is already in the scene"}})).dump();
				return;
			}

			obs_sceneitem_t *scene_item = obs_scene_add(scene_obj, source);
			if (!scene_item)
				out_jsonReturn = Json(Json::object({{"error", "Failed to add source to scene"}})).dump();

			obs_source_release(source);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_OBS_SOURCE_DESTROY(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, this, params, &out_jsonReturn]() {
			const auto &name = params["param2"].string_value();

			OBSSourceAutoRelease src = obs_get_source_by_name(name.c_str());

			if (src == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Can't find source with name " + name}})).dump();
				return;
			}

			if (obs_source_get_type(src) == OBS_SOURCE_TYPE_TRANSITION)
			{
				obs_source_remove(src);
			}
			else if (obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE)
			{
				// Function from obs ui
				auto getSceneCount = []()
				{
					size_t ret;
					auto sceneEnumProc = [](void *param, obs_source_t *scene) {
						auto ret = static_cast<size_t *>(param);

						if (obs_source_is_group(scene))
							return true;

						(*ret)++;
						return true;
					};

					obs_enum_scenes(sceneEnumProc, &ret);
					return ret;
				};

				if (getSceneCount() < 2)
				{
					out_jsonReturn = Json(Json::object({{"error", "You cannot remove the last scene in the collection."}})).dump();
					return;
				}

				blog(LOG_INFO, "Releasing scene %s", obs_source_get_name(src));
				std::list<obs_sceneitem_t *> items;
				auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
					if (item)
					{
						std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
						obs_sceneitem_addref(item);
						items->push_back(item);
					}
					return true;
				};
				obs_scene_t *scene = obs_scene_from_source(src);
				if (scene)
					obs_scene_enum_items(scene, cb, &items);

				for (auto item : items)
				{
					obs_sceneitem_remove(item);
					obs_sceneitem_release(item);
				}

				obs_source_remove(src);
			}
			else
			{
				obs_source_remove(src);
			}
		},
		Qt::BlockingQueuedConnection);
}

/***
* Save/Load
**/

// Handle the close event here before Qt sees it
LRESULT CALLBACK HandleWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CLOSE)
	{
		PluginJsHandler::instance().saveSlabsBrowserDocks();
	}

	// Allow Qt to process the message as well
	WNDPROC origWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	return CallWindowProc(origWndProc, hwnd, uMsg, wParam, lParam);
}

void PluginJsHandler::saveSlabsBrowserDocks()
{
	Json::array jarray;
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();

	foreach(QDockWidget * dock, docks)
	{
		if (dock->property("isSlabs").isValid())
		{
			QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();

			std::string url;

			if (widget->cefBrowser != nullptr)
				url = widget->cefBrowser->GetMainFrame()->GetURL();

			Json::object obj{
				{"title", dock->windowTitle().toStdString()},
				{"url", url},
				{"objectName", dock->objectName().toStdString()},
			};

			jarray.push_back(obj);
		}
	}

	std::string output = Json(jarray).dump();
	config_set_string(obs_frontend_get_global_config(), "BasicWindow", "SlabsBrowserDocks", output.c_str());
}

void PluginJsHandler::loadSlabsBrowserDocks()
{
	// This is to intercept the shutdown event so that we can save it before OBS does anything
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	WNDPROC origWndProc = (WNDPROC)SetWindowLongPtr(reinterpret_cast<HWND>(mainWindow->winId()), GWLP_WNDPROC, (LONG_PTR)HandleWndProc);
	SetWindowLongPtr(reinterpret_cast<HWND>(mainWindow->winId()), GWLP_USERDATA, (LONG_PTR)origWndProc);

	const char *jsonStr = config_get_string(obs_frontend_get_global_config(), "BasicWindow", "SlabsBrowserDocks");

	std::string err;
	Json json = Json::parse(jsonStr, err);

	if (!err.empty())
		return;

	Json::array array = json.array_items();

	for (Json &item : array)
	{
		std::string title = item["title"].string_value();
		std::string url = item["url"].string_value();
		std::string objectName = item["objectName"].string_value();

		static QCef *qcef = obs_browser_init_panel();

		QDockWidget *dock = new QDockWidget(mainWindow);
		QCefWidget *browser = qcef->create_widget(dock, url, nullptr);
		dock->setWidget(browser);
		dock->setWindowTitle(title.c_str());
		dock->setObjectName(objectName.c_str());
		dock->setProperty("isSlabs", true);
		dock->setObjectName(objectName.c_str());

		// obs_frontend_add_dock and keep the pointer to it
		dock->setProperty("actionptr", (uint64_t)obs_frontend_add_dock(dock));

		dock->resize(460, 600);
		dock->setMinimumSize(80, 80);
		dock->setWindowTitle(title.c_str());
		dock->setAllowedAreas(Qt::AllDockWidgetAreas);
		dock->setWidget(browser);
	}
}

void PluginJsHandler::JS_GET_SCENE_COLLECTIONS(const json11::Json& params, std::string& out_jsonReturn)
{
	char **scene_collections = obs_frontend_get_scene_collections();

	std::vector<Json> result;

	for (int i = 0; scene_collections[i] != nullptr; ++i)
		result.push_back(Json::object{{"name", scene_collections[i]}});

	// Convert the panelInfo vector to a Json object and dump string
	Json ret = result;
	out_jsonReturn = ret.dump();
}

void PluginJsHandler::JS_GET_CURRENT_SCENE_COLLECTION(const json11::Json &params, std::string &out_jsonReturn)
{
	Json ret = Json::object{{"name", obs_frontend_get_current_scene_collection()}};
	out_jsonReturn = ret.dump();
}

void PluginJsHandler::JS_SET_CURRENT_SCENE_COLLECTION(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &name = params["param2"].string_value();

	if (name.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid param"}})).dump();
		return;
	}

	obs_frontend_set_current_scene_collection(name.c_str());
}

void PluginJsHandler::JS_ADD_SCENE_COLLECTION(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &name = params["param2"].string_value();

	if (name.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid param"}})).dump();
		return;
	}

	if (!obs_frontend_add_scene_collection(name.c_str()))
		out_jsonReturn = Json(Json::object({{"error", "Obs function failed"}})).dump();
	else
		out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
}

void PluginJsHandler::JS_SET_SCENEITEM_POS(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];
	const auto &param5Value = params["param5"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	float x = (float)param4Value.number_value();
	float y = (float)param5Value.number_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, scene_name, source_name, x, y, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed find the source in that scene"}})).dump();
					return;
				}

				vec2 pos;
				pos.x = x;
				pos.y = y;
				obs_sceneitem_set_pos(scene_item, &pos);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_SCENEITEM_ROT(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	float rotation = (float)param4Value.number_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, rotation, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed find the source in that scene"}})).dump();
					return;
				}

				obs_sceneitem_set_rot(scene_item, rotation);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_SCENEITEM_CROP(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];
	const auto &param5Value = params["param5"];
	const auto &param6Value = params["param6"];
	const auto &param7Value = params["param7"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	int left = param4Value.int_value();
	int top = param5Value.int_value();
	int right = param6Value.int_value();
	int bottom = param7Value.int_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, left, top, right, bottom, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed find the source in that scene"}})).dump();
					return;
				}

				struct obs_sceneitem_crop crop = {left, top, right, bottom};
				obs_sceneitem_set_crop(scene_item, &crop);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_SCENEITEM_SCALE_FILTER(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	int scale_type = param4Value.int_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, scale_type, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				obs_sceneitem_set_scale_filter(scene_item, (obs_scale_type)scale_type);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_SCENEITEM_BLENDING_MODE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	int blending_type = param4Value.int_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, blending_type, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				obs_sceneitem_set_blending_mode(scene_item, (obs_blending_type)blending_type);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_SCENEITEM_BLENDING_METHOD(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	int blending_method = param4Value.int_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, blending_method, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_set_blending_method exists and accepts an enum type for blending method.
				obs_sceneitem_set_blending_method(scene_item, (obs_blending_method)blending_method);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_SCALE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];
	const auto &param5Value = params["param5"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();
	float x_scale = (float)param4Value.number_value();
	float y_scale = (float)param5Value.number_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, x_scale, y_scale, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_set_scale exists and can set x and y scale values.
				vec2 scale = {x_scale, y_scale};
				obs_sceneitem_set_scale(scene_item, &scale);
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SCENEITEM_POS(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_pos exists and retrieves x and y position values.
				vec2 position;
				obs_sceneitem_get_pos(scene_item, &position);
				out_jsonReturn = Json(Json::object({{"x", position.x}, {"y", position.y}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SCENEITEM_ROT(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_rot exists and retrieves the rotation value.
				float rotation = obs_sceneitem_get_rot(scene_item);
				out_jsonReturn = Json(Json::object({{"rotation", rotation}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SCENEITEM_CROP(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_crop exists and retrieves the crop values.
				obs_sceneitem_crop crop_values;
				obs_sceneitem_get_crop(scene_item, &crop_values);

				out_jsonReturn = Json(Json::object({{"left", crop_values.left}, {"right", crop_values.right}, {"top", crop_values.top}, {"bottom", crop_values.bottom}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SOURCE_DIMENSIONS(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string source_name = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease source = obs_get_source_by_name(source_name.c_str());
			if (!source)
			{
				out_jsonReturn = Json(Json::object({{"error", "Did not find a source with name " + source_name}})).dump();
				return;
			}

			uint32_t width = obs_source_get_width(source);
			uint32_t height = obs_source_get_height(source);

			out_jsonReturn = Json(Json::object({{"width", static_cast<int>(width)}, {"height", static_cast<int>(height)}})).dump();
		},
		Qt::BlockingQueuedConnection);
}


void PluginJsHandler::JS_GET_SCALE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_scale exists and retrieves the scale values.
				vec2 scale_values;
				obs_sceneitem_get_scale(scene_item, &scale_values);

				out_jsonReturn = Json(Json::object({{"x", scale_values.x}, {"y", scale_values.y}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SCENEITEM_SCALE_FILTER(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_scale_filter exists and retrieves the scale filter value.
				int scale_filter = static_cast<int>(obs_sceneitem_get_scale_filter(scene_item));
				out_jsonReturn = Json(Json::object({{"scale_filter", scale_filter}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SCENEITEM_BLENDING_MODE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_blending_mode exists and retrieves the blending mode value.
				int blending_mode = static_cast<int>(obs_sceneitem_get_blending_mode(scene_item));
				out_jsonReturn = Json(Json::object({{"blending_mode", blending_mode}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_SCENEITEM_BLENDING_METHOD(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string scene_name = param2Value.string_value();
	std::string source_name = param3Value.string_value();

	if (scene_name == source_name)
	{
		out_jsonReturn = Json(Json::object({{"error", "Scene and source inputs have same name"}})).dump();
		return;
	}

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, source_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
			else if (!obs_source_is_scene(scene))
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
			else
			{
				obs_scene_t *scene_obj = obs_scene_from_source(scene);
				obs_sceneitem_t *scene_item = obs_scene_find_source(scene_obj, source_name.c_str());

				if (!scene_item)
				{
					out_jsonReturn = Json(Json::object({{"error", "Failed to find the source in that scene"}})).dump();
					return;
				}

				// Assuming obs_sceneitem_get_blending_method exists and retrieves the blending method value.
				int blending_method = static_cast<int>(obs_sceneitem_get_blending_method(scene_item));
				out_jsonReturn = Json(Json::object({{"blending_method", blending_method}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SCENE_GET_SOURCES(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string scene_name = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[scene_name, &out_jsonReturn]() {
			OBSSourceAutoRelease scene = obs_get_source_by_name(scene_name.c_str());
			if (!scene)
			{
				out_jsonReturn = Json(Json::object({{"error", "Did not find an object with name " + scene_name}})).dump();
				return;
			}
			else if (!obs_source_is_scene(scene))
			{
				out_jsonReturn = Json(Json::object({{"error", "The object found is not a scene"}})).dump();
				return;
			}

			obs_scene_t *scene_obj = obs_scene_from_source(scene);

			std::vector<std::string> source_names;

			obs_scene_enum_items(
				scene_obj,
				[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
					OBSSourceAutoRelease source = obs_sceneitem_get_source(item);
					if (source)
					{
						std::vector<std::string> *names = reinterpret_cast<std::vector<std::string> *>(param);
						names->push_back(obs_source_get_name(source));
					}
					return true; 
				},
				&source_names);

			out_jsonReturn = Json(Json::object({{"source_names", source_names}})).dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_QUERY_ALL_SOURCES(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[&out_jsonReturn]() {
			std::vector<json11::Json> sourcesList;

			obs_enum_sources(
				[](void *param, obs_source_t *source) -> bool {
					std::vector<json11::Json> *sourcesList = reinterpret_cast<std::vector<json11::Json> *>(param);
					
					json11::Json sourceInfo = json11::Json::object({
						{"name", obs_source_get_name(source)},
						{"type", static_cast<int>(obs_source_get_type(source))},
						{"id", obs_source_get_id(source)},
						});

					sourcesList->push_back(sourceInfo);
					return true; // Continue enumeration
				},
				&sourcesList);

			out_jsonReturn = json11::Json(sourcesList).dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_CANVAS_DIMENSIONS(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[&out_jsonReturn]() {
			obs_video_info ovi;
			if (obs_get_video_info(&ovi))
			{
				uint32_t canvas_width = ovi.base_width;
				uint32_t canvas_height = ovi.base_height;
				out_jsonReturn = Json(Json::object({{"width", static_cast<int>(canvas_width)}, {"height", static_cast<int>(canvas_height)}})).dump();
			}
			else
			{
				out_jsonReturn = Json(Json::object({{"error", "Failed to get canvas dimensions"}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}
