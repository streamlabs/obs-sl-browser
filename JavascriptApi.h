#pragma once

#include <string>
#include <map>

class JavascriptApi
{
public:
	enum JSFuncs
	{
		JS_INVALID = 0,
		JS_DOCK_EXECUTEJAVASCRIPT,
		JS_DOCK_SETURL,
		JS_QUERY_DOCKS,
		JS_DOWNLOAD_ZIP,
		JS_DOWNLOAD_FILE,
		JS_INSTALL_FONT,
		JS_READ_FILE,
		JS_DELETE_FILES,
		JS_DROP_FOLDER,
		JS_QUERY_DOWNLOADS_FOLDER,
		JS_OBS_SOURCE_CREATE,
		JS_OBS_SOURCE_DESTROY,
		JS_DOCK_SETAREA,
		JS_DOCK_RESIZE,
		JS_DOCK_SWAP,
		JS_DOCK_NEW_BROWSER_DOCK,
		JS_GET_MAIN_WINDOW_GEOMETRY,
		JS_TOGGLE_USER_INPUT,
		JS_TOGGLE_DOCK_VISIBILITY,
		JS_DESTROY_DOCK,
		JS_DOCK_RENAME,
		JS_DOCK_SETTITLE,
		JS_SET_STREAMSETTINGS,
		JS_GET_STREAMSETTINGS,
		JS_START_WEBSERVER,
		JS_STOP_WEBSERVER,
		JS_LAUNCH_OS_BROWSER_URL,
		JS_GET_AUTH_TOKEN,
		JS_CLEAR_AUTH_TOKEN,
		JS_SET_CURRENT_SCENE,
		JS_GET_CURRENT_SCENE,
		JS_CREATE_SCENE,
		JS_SCENE_ADD,
		JS_SCENE_GET_SOURCES,
		JS_QUERY_ALL_SOURCES,
		JS_QUERY_SCENES,
		JS_SOURCE_GET_PROPERTIES,
		JS_SOURCE_GET_SETTINGS,
		JS_SOURCE_SET_SETTINGS,
		JS_GET_SCENE_COLLECTIONS,
		JS_GET_CURRENT_SCENE_COLLECTION,
		JS_SET_CURRENT_SCENE_COLLECTION,
		JS_ADD_SCENE_COLLECTION,		
		JS_SET_SCENEITEM_POS,
		JS_SET_SCENEITEM_ROT,
		JS_SET_SCENEITEM_CROP,
		JS_SET_SCENEITEM_SCALE_FILTER,
		JS_SET_SCENEITEM_BLENDING_MODE,
		JS_SET_SCENEITEM_BLENDING_METHOD,
		JS_SET_SCALE,				
		JS_GET_SCENEITEM_POS,
		JS_GET_SCENEITEM_ROT,
		JS_GET_SCENEITEM_CROP,
		JS_GET_SCENEITEM_SCALE_FILTER,
		JS_GET_SCENEITEM_BLENDING_MODE,
		JS_GET_SCENEITEM_BLENDING_METHOD,
		JS_GET_SCALE,
		JS_GET_SOURCE_DIMENSIONS,
		JS_GET_CANVAS_DIMENSIONS,
		JS_QT_RESIZE_BROWSER,
		JS_OBS_BRING_FRONT,
		JS_QT_BRING_FRONT,
		JS_QT_SET_WINDOW_POSITION,
		JS_OBS_TOGGLE_HIDE_SELF,
		JS_QT_SET_ALLOW_HIDE_BROWSER,
		JS_OBS_ADD_TRANSITION,
		JS_OBS_SET_CURRENT_TRANSITION,
		JS_OBS_REMOVE_TRANSITION,
		JS_GET_IS_OBS_STREAMING,
		JS_TRANSITION_GET_SETTINGS,
		JS_TRANSITION_SET_SETTINGS,
		JS_ENUM_SCENES,
		JS_RESTART_OBS,
		JS_SL_VERSION_INFO,
	};

public:

	// Control over the plugin/OBS side
	static std::map<std::string, JSFuncs> &getPluginFunctionNames()
	{
		// None of the api function belows are blocking, they return immediatelly, but can accept a function as arg1 thats invoked when work is complete, which should allow await/promise structure
		static std::map<std::string, JSFuncs> names =
		{
			/***
			* Docks
			*/

			// .(@function(arg1))
			//	Example arg1 = [{ "objectName": ".", "x": 0, "y": 0, "width": 0, "height": 0, "isSlabs": bool, "floating": bool, "url": ".", "visible": ".", "title": "." }]
			{"dock_queryAll", JS_QUERY_DOCKS},

			// .(@function(arg1), @objectName, @url)
			//	Only works on docks we've created
			{"dock_setURL", JS_DOCK_SETURL},

			// .(@function(arg1), @objectName, @jsString)
			//	Only works on docks we've created
			{"dock_executeJavascript", JS_DOCK_EXECUTEJAVASCRIPT},

			// .(@function(arg1), @objectName, @bool_visible)
			{"dock_toggleDockVisibility", JS_TOGGLE_DOCK_VISIBILITY},

			// Current release, OBS 29.1, does not have api support for destroying docks. Futurue releases will.
			// .(@objectName)
			//	Only works on docks we've created
			{"dock_destroyBrowserDock", JS_DESTROY_DOCK},

			// .(@function(arg1), @title, @url, @objectName)
			//	Creates a new browser dock, its guid is the 'objectName', title is what the user sees. Will appear in their list of docks but not as a "Browser Dock", even though it works identically as one
			//		objectName is the unique identifer of the dock
			{"dock_newBrowserDock", JS_DOCK_NEW_BROWSER_DOCK},

			// .(@function(arg1), @objectName, @int_areaMask)
			//	areaMask can be a combination of Left Right Top Bottom, ie (LeftDockWidgetArea | RightDockWidgetArea) or (TopDockWidgetArea | BottomDockWidgetArea)
			//	These are the current values from Qt
			//		LeftDockWidgetArea = 0x1,
			//		RightDockWidgetArea = 0x2,
			//		TopDockWidgetArea = 0x4,
			//		BottomDockWidgetArea = 0x8,
			//	If the dock is floating then this will set that to false and place it somewhere
			{"dock_setArea", JS_DOCK_SETAREA},

			// .(@function(arg1), @objectName, @int_width, @int_height)
			//	Calls Qt 'resize' on the dock in question with w/h
			{"dock_resize", JS_DOCK_RESIZE},

			// .(@function(arg1), @objectName1, @objectName2)
			//	Swaps the the positions of dock1 with dock2
			{"dock_swap", JS_DOCK_SWAP},

			// .(@function(arg1), @objectName, @newName)
			//	Renames a dock's objectName, which must be unique and not match any other existing
			{"dock_rename", JS_DOCK_RENAME},

			// .(@function(arg1), @objectName, @newTitle)
			//	Renames a dock's objectName, which must be unique and not match any other existing
			{"dock_setTitle", JS_DOCK_SETTITLE},

			/***
			* Qt Information
			*/

			// .(@function(arg1))
			//	Returns the screen x,y and width/height of the main window
			//		Example arg1 = { "x": ".", "y": ".", "width": ".", "height": "." }
			{"qt_getMainWindowGeometry", JS_GET_MAIN_WINDOW_GEOMETRY},

			/***
			* Windows
			*/

			// .(@function(arg1), @bool_enable)
			//	Disable/Enable user input to the window
			{"win_toggleUserInput", JS_TOGGLE_USER_INPUT},

			// .(@function(arg1))
			//	Launches a new OBS and terminates existing one at same time.
			{"win_restartOBS", JS_RESTART_OBS},

			/***
			* Filesystem
			*/

			// .(@function(arg1), @url)
			//	Downloads and unpacks the zip, returning a list of full file paths to the files that were in it
			//		Example arg1 = [{ "path": "..." },]
			{"fs_downloadZip", JS_DOWNLOAD_ZIP},

			// .(@function(arg1), @url, @filename)
			//	Downloads file, returning a filepath to it
			//		Example arg1 = { "path": "..." }
			{"fs_downloadFile", JS_DOWNLOAD_FILE},

			// .(@function(arg1), @filepath)
			//	Performs 'AddFontResourceA' from the WinApi to the filepath in question
			{"fs_installFont", JS_INSTALL_FONT},
			
			// .(@function(arg1), @filepath)
			//	Returns the contents of a file as a string. If the filesize is over 1mb this will return an error
			//		Example arg1 = { "contents": "..." }
			{"fs_readFile", JS_READ_FILE},

			// .(@function(arg1), @filepaths_jsonStr)
			//	Json string, array, [{ path: "..." },] paths must be relative to the streamlabs download folder, ie "/download1234/file.png"
			{"fs_deleteFiles", JS_DELETE_FILES},

			// .(@function(arg1), @path)
			//	Path must be relative to the streamlabs download folder, ie "/download1234/"
			{"fs_dropFolder", JS_DROP_FOLDER},

			// .(@function(arg1))
			//	Returns comprehensive list of everything in our downloads folder
			//		Example arg1 = [{ "path": "..." },]
			{"fs_queryDownloadsFolder", JS_QUERY_DOWNLOADS_FOLDER},

			/***
			* obs
			*/

			// .(@function(arg1), @id, @name, @settings_jsonStr, @hotkey_data_jsonStr)
			//	Creates an obs source, also returns back some information about the source you just created if you want it
			//	Note that 'name' is also the guid of it, duplicates can't exist
			//		Example arg1 = { "settings_jsonStr": "obs_data_get_full_json()", "audio_mixers": "obs_source_get_audio_mixers()", "deinterlace_mode": "obs_source_get_deinterlace_mode()", "deinterlace_field_order": "obs_source_get_deinterlace_field_order()" }
			{"obs_source_create", JS_OBS_SOURCE_CREATE},

			// .(@function(arg1), name)
			//	Destroys an obs source with the name provided if it exists via obs_source_remove(name)
			//	NOTE: Can be used to destroy scenes/transition/colletions, as they are types of sources. In the case of scenes or scene colletions, obs_sceneitem_remove/obs_sceneitem_release on sources belonging to it akin to OSN scene remove
			{"obs_source_destroy", JS_OBS_SOURCE_DESTROY},

			// .(@function(arg1), @service, @protocol, @server, @bool_use_auth, @username, @password, @key)
			//	Revises stream settings with the provided params
			//		'service' can be "rtmp_custom" : "rtmp_common"
			{"obs_set_stream_settings", JS_SET_STREAMSETTINGS},

			// .(@function(arg1))
			//	Returns json of service, protocol, server, bool_use_auth, username, password, key
			{"obs_get_stream_settings", JS_GET_STREAMSETTINGS},

			// .(@function(arg1), @sceneName)
			//	Performs 'obs_frontend_set_current_scene' on the scene in question
			{"obs_set_current_scene", JS_SET_CURRENT_SCENE},

			// .(@function(arg1))
			//		Example arg1 = { "name": "." }
			{"obs_get_current_scene", JS_GET_CURRENT_SCENE},

			// .(@function(arg1), @sceneName)
			//	Peforms literally obs_scene_create(sceneName) 
			{"obs_create_scene", JS_CREATE_SCENE},

			// .(@function(arg1), @sceneName, @sourceName)
			//	Peforms literally obs_scene_add(sceneName, sourceName)
			{"obs_scene_add", JS_SCENE_ADD},

			// .(@function(arg1), @sceneName)
			//		Example arg1 = { "source_names": [] }
			{"obs_scene_get_sources", JS_SCENE_GET_SOURCES},

			// .(@function(arg1))
			//	OBS_SOURCE_TYPE_INPUT = 0
			//	OBS_SOURCE_TYPE_FILTER = 1
			//		Example arg1 = [ { "name": ".", "type": 0, "id": "." }, ... ]
			//
			//	Tansition/scene are sources yet may not be a part of obs_enum_sources
			//		OBS_SOURCE_TYPE_TRANSITION = 2
			//		OBS_SOURCE_TYPE_SCENE = 3
			{"obs_query_all_sources", JS_QUERY_ALL_SOURCES},

			// .(@function(arg1))
			//		Example arg1 = [ { "name": ".", "type": 0, "id": "." }, ... ]
			{"obs_enum_scenes", JS_ENUM_SCENES},

			// .(@function(arg1)
			//	Not yet implemented
			{"obs_source_get_properties_json", JS_SOURCE_GET_PROPERTIES},

			// .(@function(arg1), @sourceName)
			//	Iterates the settings of a source and returns them as a json strong
			//		Example arg1 = <settings>
			{"obs_source_get_settings_json", JS_SOURCE_GET_SETTINGS},

			// .(@function(arg1), @json_settings, @sourceName)
			//	Applies the json data into the source settings
			{"obs_source_set_settings_json", JS_SOURCE_SET_SETTINGS},
			
			// .(@function(arg1))
			//		Example arg1 = [{ "name": "..." },]
			{"obs_get_scene_collections", JS_GET_SCENE_COLLECTIONS},

			// .(@function(arg1))
			//		Example arg1 = [{ "name": "..." }
			{"obs_get_current_scene_collection", JS_GET_CURRENT_SCENE_COLLECTION},

			// .(@function(arg1), @sceneName)
			{"obs_set_current_scene_collection", JS_SET_CURRENT_SCENE_COLLECTION},

			// .(@function(arg1), @sceneName)
			{"obs_add_scene_collection", JS_ADD_SCENE_COLLECTION},

			// .(@function(arg1), @sceneName, @sourceName, @decimal_x, @decimal_y)
			{"obs_sceneitem_set_pos", JS_SET_SCENEITEM_POS},

			// .(@function(arg1), @sceneName, @sourceName, @decimal_rot)
			{"obs_sceneitem_set_rot", JS_SET_SCENEITEM_ROT},

			// .(@function(arg1), @sceneName, @sourceName, @int_left, @int_top, @int_right, @int_bottom)
			{"obs_sceneitem_set_crop", JS_SET_SCENEITEM_CROP},

			// .(@function(arg1), @sceneName, @sourceName, @decimal_x, @decimal_y)
			{"obs_sceneitem_set_scale", JS_SET_SCALE},

			// .(@function(arg1), @sceneName, @sourceName, @int_scaleType)
			//	OBS_SCALE_DISABLE = 1
			//	OBS_SCALE_POINT = 2
			//	OBS_SCALE_BICUBIC = 3
			//	OBS_SCALE_BILINEAR = 4
			//	OBS_SCALE_LANCZOS = 5
			//	OBS_SCALE_AREA = 6
			{"obs_sceneitem_set_scale_filter", JS_SET_SCENEITEM_SCALE_FILTER},

			// .(@function(arg1), @sceneName, @sourceName, @int_blendingType)
			//	OBS_BLEND_NORMAL = 1
			//	OBS_BLEND_ADDITIVE = 2
			//	OBS_BLEND_SUBTRACT = 3
			//	OBS_BLEND_SCREEN = 4
			//	OBS_BLEND_MULTIPLY = 5
			//	OBS_BLEND_LIGHTEN = 6
			//	OBS_BLEND_DARKEN = 7
			{"obs_sceneitem_set_blending_mode", JS_SET_SCENEITEM_BLENDING_MODE},

			// .(@function(arg1), @sceneName, @sourceName, @int_blendingMethod)
			//	OBS_BLEND_METHOD_DEFAULT = 1
			//	OBS_BLEND_METHOD_SRGB_OFF = 2
			{"obs_sceneitem_set_blending_method", JS_SET_SCENEITEM_BLENDING_METHOD},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "x": 0.0, "y": 0.0 }
			{"obs_sceneitem_get_pos", JS_GET_SCENEITEM_POS},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "rotation": 0.0 }
			{"obs_sceneitem_get_rot", JS_GET_SCENEITEM_ROT},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "left": 0.0, "right": 0.0, "top": 0.0, "bottom": 0.0 }
			{"obs_sceneitem_get_crop", JS_GET_SCENEITEM_CROP},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "x": 0.0, "y": 0.0 }
			{"obs_sceneitem_get_scale", JS_GET_SCALE},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "scale_filter": 0 }
			{"obs_sceneitem_get_scale_filter", JS_GET_SCENEITEM_SCALE_FILTER},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "blending_mode": 0 }
			{"obs_sceneitem_get_blending_mode", JS_GET_SCENEITEM_BLENDING_MODE},

			// .(@function(arg1), @sceneName, @sourceName)
			//		Example arg1 = { "blending_method": 0 }
			{"obs_sceneitem_get_blending_method", JS_GET_SCENEITEM_BLENDING_METHOD},

			// .(@function(arg1), @sourceName)
			//		Example arg1 = { "width": 0, "height": 0 }
			{"obs_source_get_dimensions", JS_GET_SOURCE_DIMENSIONS},

			// .(@function(arg1))
			//		Example arg1 = { "width": 0, "height": 0 }
			{"obs_canvas_get_dimensions", JS_GET_CANVAS_DIMENSIONS},

			// .(@function(arg1))
			{"obs_bring_front", JS_OBS_BRING_FRONT},

			// .(@function(arg1), @bool_hide)
			{"obs_toggle_hide_self", JS_OBS_TOGGLE_HIDE_SELF},

			// .(@function(arg1), @id, @sourceName)
			//	@id must be correct or application will crash, ie "swipe_transition" is one example
			{"obs_add_transition", JS_OBS_ADD_TRANSITION},

			// .(@function(arg1), @sourceName)
			{"obs_set_current_transition", JS_OBS_SET_CURRENT_TRANSITION},

			// .(@function(arg1), @sourceName)
			{"obs_remove_transition", JS_OBS_REMOVE_TRANSITION},

			// .(@function(arg1), @sourceName)
			//	Iterates the settings of a source and returns them as a json strong
			//		Example arg1 = <settings>
			{"obs_transition_get_settings_json", JS_TRANSITION_GET_SETTINGS},

			// .(@function(arg1), @json_settings, @sourceName)
			//	Applies the json data into the source settings
			{"obs_transition_set_settings_json", JS_TRANSITION_SET_SETTINGS},

			// .(@function(arg1))
			//	Returns the boolean value of the named obs function
			//		Example arg1 = { "value": true }
			{"obs_frontend_streaming_active", JS_GET_IS_OBS_STREAMING},

			/***
			* Web
			*/

			// .(@function(arg1), port, expectedReferer, redirectUrl)
			//	Only one can exist at a time (do we need multiple? lmk)
			//	'port', ie http://localhost:port, if you assign port 0 then the OS will choose one (value is returned in function arg1)
			//	'expectedReferer' is for example, '/?secret_token=', equates to 'http://localhost:port/?secret_token=', aka the referring uri that the webserver is will pluck the token from
			//	'redirectUrl' is where you want them to be redirected to whenever accessing 'http://localhost:port'
			//		Example arg1 = { "port": 12345 }
			{"web_startServer", JS_START_WEBSERVER},

			// .(@function(arg1))
			//	Stops the webserver
			{"web_stopServer", JS_STOP_WEBSERVER},

			// .(@function(arg1), @url)
			//	Launches their default browser with the URL supplied using ShellExecuteA, any errors returned are according to ShellExecuteA winapi doc
			{"web_launchOSBrowserUrl", JS_LAUNCH_OS_BROWSER_URL},

			// .(@function(arg1))
			//		Example arg1 = { "token": "." }
			{"web_getAuthToken", JS_GET_AUTH_TOKEN},

			// .(@function(arg1))
			{"web_clearAuthToken", JS_CLEAR_AUTH_TOKEN},

			/***
			* Streamlabs
			*/

			// .(@function(arg1))
			//		Example arg1 = { "branch": '29.1.0', "git_sha": 'abcdefg...' }
			{"sl_getVersionInfo", JS_SL_VERSION_INFO},

		};

		return names;
	}

	// Control over our the browser
	static std::map<std::string, JSFuncs> &getBrowserFunctionNames()
	{
		// None of the api function belows are blocking, they return immediatelly, but can accept a function as arg1 thats invoked when work is complete, which should allow await/promise structure
		static std::map<std::string, JSFuncs> names =
		{
			/**
			* Qt
			*/
			
			// .(@function(arg1), x, y)`
			{"qt_resizeBrowser", JS_QT_RESIZE_BROWSER},

			// .(@function(arg1))`
			{"qt_bringToFront", JS_QT_BRING_FRONT},

			// .(@function(arg1), x, y)`
			{"qt_setWindowPosition", JS_QT_SET_WINDOW_POSITION},

			// .(@function(arg1), x, y)`
			{"qt_setAllowHideBrowser", JS_QT_SET_ALLOW_HIDE_BROWSER},
		};

		return names;
	}

	static bool isValidFunctionName(const std::string &str)
	{
		return isPluginFunctionName(str) || isBrowserFunctionName(str); 
	}

	static bool isPluginFunctionName(const std::string &str)
	{
		auto ref = getPluginFunctionNames();
		return ref.find(str) != ref.end();
	}

	static bool isBrowserFunctionName(const std::string &str)
	{
		auto ref = getBrowserFunctionNames();
		return ref.find(str) != ref.end();
	}

	static JSFuncs getFunctionId(const std::string &funcName)
	{
		auto ref = getPluginFunctionNames();
		auto itr = ref.find(funcName);

		if (itr != ref.end())
			return itr->second;

		ref = getBrowserFunctionNames();
		itr = ref.find(funcName);

		if (itr != ref.end())
			return itr->second;

		return JS_INVALID;
	}
};
