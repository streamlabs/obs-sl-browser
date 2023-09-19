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
	};

public:
	static std::map<std::string, JSFuncs> &getGlobalFunctionNames()
	{
		// None of the api function belows are blocking, they return immediatelly, but can accept a function as arg1 thats invoked when work is complete, which should allow await/promise structure
		static std::map<std::string, JSFuncs> names = {
			/***
			* Docks
			*/

			// .(@function(arg1))
			//	Example arg1 = [{ "objectName": ".", "x": 0, "y": 0, "width": 0, "height": 0, "isSlabs": bool, "floating": bool, "url": ".", "visible": "." }]
			{"dock_queryAll", JS_QUERY_DOCKS},

			// .(@objectName, @url)
			//	Only works on docks we've created
			{"dock_setURL", JS_DOCK_SETURL},

			// .(@objectName, @jsString)
			//	Only works on docks we've created
			{"dock_executeJavascript", JS_DOCK_EXECUTEJAVASCRIPT},

			// .(@objectName, @bool_visible)
			{"dock_toggleDockVisibility", JS_TOGGLE_DOCK_VISIBILITY},

			// Current release, OBS 29.1, does not have api support for destroying docks. Futurue releases will.
			// .(@objectName)
			//	Only works on docks we've created
			{"dock_destroyBrowserDock", JS_DESTROY_DOCK},

			// .(@title, @url, @objectName)
			//	Creates a new browser dock, its guid is the 'objectName', title is what the user sees. Will appear in their list of docks but not as a "Browser Dock", even though it works identically as one
			//		objectName is the unique identifer of the dock
			{"dock_newBrowserDock", JS_DOCK_NEW_BROWSER_DOCK},

			// .(@objectName, @int_areaMask)
			//	areaMask can be a combination of Left Right Top Bottom, ie (LeftDockWidgetArea | RightDockWidgetArea) or (TopDockWidgetArea | BottomDockWidgetArea)
			//	These are the current values from Qt
			//		LeftDockWidgetArea = 0x1,
			//		RightDockWidgetArea = 0x2,
			//		TopDockWidgetArea = 0x4,
			//		BottomDockWidgetArea = 0x8,
			//	If the dock is floating then this will set that to false and place it somewhere
			{"dock_setArea", JS_DOCK_SETAREA},

			// .(@objectName, @int_width, @int_height)
			//	Calls Qt 'resize' on the dock in question with w/h
			{"dock_resize", JS_DOCK_RESIZE},

			// .(@objectName1, @objectName2)
			//	Swaps the the positions of dock1 with dock2
			{"dock_swap", JS_DOCK_SWAP},

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

			// .(bool_enable)
			//	Disable/Enable user input to the window
			{"win_toggleUserInput", JS_TOGGLE_USER_INPUT},

			/***
			* Filesystem
			*/

			// .(@function(arg1), @url)
			//	Downloads and unpacks the zip, returning a list of full file paths to the files that were in it
			//		Example arg1 = [{ "path": "..." },]
			{"fs_downloadZip", JS_DOWNLOAD_ZIP},

			// .(@function(arg1), @filepath)
			//	Returns the contents of a file as a string. If the filesize is over 1mb this will return an error
			//		Example arg1 = { "contents": "..." }
			{"fs_readFile", JS_READ_FILE},

			// .(@filepaths_jsonStr)
			//	Json string, array, [{ path: "..." },] paths must be relative to the streamlabs download folder, ie "/download1234/file.png"
			{"fs_deleteFiles", JS_DELETE_FILES},

			// .(@path)
			//	Path must be relative to the streamlabs download folder, ie "/download1234/"
			{"fs_dropFolder", JS_DROP_FOLDER},

			// .(@function(arg1))
			//	Returns comprehensive list of everything in our downloads folder
			//		Example arg1 = [{ "path": "..." },]
			{"fs_queryDownloadsFolder", JS_QUERY_DOWNLOADS_FOLDER},

			/***
			* obs_source
			*/

			// .(@function(arg1), id, name, settings_jsonStr, hotkey_data_jsonStr)
			//	Creates an obs source, also returns back some information about the source you just created if you want it
			//	Note that 'name' is also the guid of it, duplicates can't exist
			//		Example arg1 = { "settings_jsonStr": "obs_data_get_full_json()", "audio_mixers": "obs_source_get_audio_mixers()", "deinterlace_mode": "obs_source_get_deinterlace_mode()", "deinterlace_field_order": "obs_source_get_deinterlace_field_order()" }
			{"obs_source_create", JS_OBS_SOURCE_CREATE},

			// .(@function(arg1), name)
			//	Destroys an obs source with the name provided if it exists
			{"obs_source_destroy", JS_OBS_SOURCE_DESTROY},

		};

		return names;
	}

	static bool isValidFunctionName(const std::string &str)
	{
		auto ref = getGlobalFunctionNames();
		return ref.find(str) != ref.end();
	}

	static JSFuncs getFunctionId(const std::string &funcName)
	{
		auto ref = getGlobalFunctionNames();
		auto itr = ref.find(funcName);

		if (itr != ref.end())
			return itr->second;

		return JS_INVALID;
	}
};
