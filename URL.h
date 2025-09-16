#pragma once

namespace URL
{
	static std::string getPluginHttpUrl()
	{
		char buffer[MAX_PATH];
		DWORD len = GetEnvironmentVariableA("SL_PLUGIN_DEFAULT_URL", buffer, MAX_PATH);

		if (len > 0 && len < MAX_PATH)
			return buffer; 

		return "https://obs-plugin.streamlabs.com";
	}
};
