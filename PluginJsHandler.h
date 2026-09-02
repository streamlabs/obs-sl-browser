#pragma once

#include <mutex>
#include <thread>
#include <vector>
#include <map>
#include <string>
#include <filesystem>
#include <obs.h>
#include <Windows.h>

#include <QStringList>

#include <json11/json11.hpp>

class QDockWidget;

class PluginJsHandler
{
public:
	void start();
	void stop();
	void pushApiRequest(const std::string &funcName, const std::string &params);
	void executeApiRequest(const std::string &funcName, const std::string &params);
	void loadSlabsBrowserDocks();
	void saveSlabsBrowserDocks();
	void loadFonts();
	void onWmClose();

	static void handle_obs_frontend_event(enum obs_frontend_event event, void *data);

public:
	static PluginJsHandler &instance()
	{
		static PluginJsHandler a;
		return a;
	}

private:
	PluginJsHandler();
	~PluginJsHandler();

	void workerThread();
	void freezeCheckThread();

	void JS_QUERY_DOCKS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_EXECUTEJAVASCRIPT(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_SETURL(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOWNLOAD_ZIP(const json11::Json &params, std::string &out_jsonReturn);
	void JS_RUN_STREAMLABS_EXE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOWNLOAD_FILE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_READ_FILE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DELETE_FILES(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DROP_FOLDER(const json11::Json &params, std::string &out_jsonReturn);
	void JS_QUERY_DOWNLOADS_FOLDER(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_SOURCE_CREATE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_SOURCE_DESTROY(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_SETAREA(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_RESIZE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_NEW_BROWSER_DOCK(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_MAIN_WINDOW_GEOMETRY(const json11::Json &params, std::string &out_jsonReturn);
	void JS_TOGGLE_USER_INPUT(const json11::Json &params, std::string &out_jsonReturn);
	void JS_TOGGLE_DOCK_VISIBILITY(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_SWAP(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DESTROY_DOCK(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_RENAME(const json11::Json &params, std::string &out_jsonReturn);
	void JS_DOCK_SETTITLE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_STREAMSETTINGS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_STREAMSETTINGS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SL_VERSION_INFO(const json11::Json &params, std::string &out_jsonReturn);
	void JS_START_WEBSERVER(const json11::Json &params, std::string &out_jsonReturn);
	void JS_STOP_WEBSERVER(const json11::Json &params, std::string &out_jsonReturn);
	void JS_LAUNCH_OS_BROWSER_URL(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_AUTH_TOKEN(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_CURRENT_SCENE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_CREATE_SCENE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SCENE_ADD(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SOURCE_GET_PROPERTIES(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SOURCE_GET_SETTINGS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SOURCE_SET_SETTINGS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_INSTALL_FONT(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENE_COLLECTIONS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_CURRENT_SCENE_COLLECTION(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_CURRENT_SCENE_COLLECTION(const json11::Json &params, std::string &out_jsonReturn);
	void JS_ADD_SCENE_COLLECTION(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_POS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_ROT(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_VISIBILITY(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_CROP(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_SCALE_FILTER(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_BLENDING_MODE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCENEITEM_BLENDING_METHOD(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SET_SCALE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENEITEM_POS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENEITEM_ROT(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENEITEM_CROP(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCALE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENEITEM_SCALE_FILTER(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENEITEM_BLENDING_MODE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SCENEITEM_BLENDING_METHOD(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SCENE_GET_SOURCES(const json11::Json &params, std::string &out_jsonReturn);
	void JS_QUERY_ALL_SOURCES(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_SOURCE_DIMENSIONS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_CANVAS_DIMENSIONS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_CLEAR_AUTH_TOKEN(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_CURRENT_SCENE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_BRING_FRONT(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_TOGGLE_HIDE_SELF(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_ADD_TRANSITION(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_SET_CURRENT_TRANSITION(const json11::Json &params, std::string &out_jsonReturn);
	void JS_OBS_REMOVE_TRANSITION(const json11::Json &params, std::string &out_jsonReturn);
	void JS_TRANSITION_GET_SETTINGS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_TRANSITION_SET_SETTINGS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_ENUM_SCENES(const json11::Json &params, std::string &out_jsonReturn);
	void JS_RESTART_OBS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_IS_OBS_STREAMING(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SAVE_SL_BROWSER_DOCKS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_QT_SET_JS_ON_CLICK_STREAM(const json11::Json &params, std::string &out_jsonReturn);
	void JS_QT_INVOKE_CLICK_ON_STREAM_BUTTON(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_LOGS_REPORT_STRING(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SOURCE_FILTER_ADD(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SOURCE_FILTER_REMOVE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_QT_GET_COOKIE_VALUE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_BROWSERSOURCE_SEND_MESSAGE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_PATH_JOIN(const json11::Json &params, std::string &out_jsonReturn);
	void JS_GET_ENV_VAR(const json11::Json &params, std::string &out_jsonReturn);
	void JS_MKDIR(const json11::Json &params, std::string &out_jsonReturn);
	void JS_PATH_EXISTS(const json11::Json &params, std::string &out_jsonReturn);
	void JS_REMOVE_PATH(const json11::Json &params, std::string &out_jsonReturn);
	void JS_MOVE_PATH(const json11::Json &params, std::string &out_jsonReturn);
	void JS_SHA256_FILE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_WRITE_FILE(const json11::Json &params, std::string &out_jsonReturn);
	void JS_IS_PROCESS_RUNNING(const json11::Json &params, std::string &out_jsonReturn);
	void JS_STOP_PROCESS(const json11::Json &params, std::string &out_jsonReturn);

	// Internal helpers
	std::string ws_to_utf8(const std::wstring &str);
	std::wstring utf8_to_ws(const std::string &str);
	bool resolveWithinDownloads(const std::wstring &downloadsDir, const std::string &inputPath, std::filesystem::path &out_resolved, std::string &out_error);
	bool sha256File(const std::filesystem::path &filePath, std::string &out_hexDigest, std::string &out_error);

	std::wstring getDownloadsDir() const;
	std::wstring getFontsDir() const;
	HANDLE getChildJob();

	static QDockWidget *findDock(const std::string &objectName);

	std::mutex m_queueMtx;
	std::atomic<bool> m_running = false;
	std::vector<std::pair<std::string, std::string>> m_queudRequests;
	std::thread m_workerThread;
	std::thread m_freezeCheckThread;

	std::map<uint32_t, HANDLE> m_childProcesses;
	HANDLE m_childJob = nullptr;

	bool m_restartApp = false;

	std::unique_ptr<QString> m_restartProgramStr;
	std::unique_ptr<QStringList> m_restartArguments;
};
