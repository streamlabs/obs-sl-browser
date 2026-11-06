#pragma once

// Diagnostic build only: traces how App Store tab documents get their init script. Grep cef.log for [sl-diag].

#include <include/base/cef_logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <regex>
#include <string>

#define SL_DIAG LOG(WARNING) << "[sl-diag] "

namespace SlDiag
{
// Query strings carry app tokens: keep their length only. Short route fragments like "#/" stay readable
inline std::string url(const std::string &u)
{
	const size_t q = u.find('?');
	const size_t h = u.find('#');
	std::string out = u.substr(0, std::min(q, h));

	if (q != std::string::npos && (h == std::string::npos || q < h))
		out += "?<" + std::to_string((h == std::string::npos ? u.size() : h) - q - 1) + " chars>";

	if (h != std::string::npos)
	{
		const std::string fragment = u.substr(h);
		out += (fragment.find('=') != std::string::npos || fragment.size() > 40) ? "#<" + std::to_string(fragment.size() - 1) + " chars>" : fragment;
	}

	return out;
}

inline std::string hash(const std::string &s)
{
	uint32_t h = 2166136261u;
	for (unsigned char c : s)
		h = (h ^ c) * 16777619u;

	char buf[9];
	snprintf(buf, sizeof(buf), "%08x", h);
	return buf;
}

// Enough of a script to tell PIPE_CLIENT_JS from the onNavigation emit
inline std::string snippet(const std::string &s, size_t n = 70)
{
	std::string out;
	for (size_t i = 0; i < s.size() && out.size() < n; ++i)
	{
		const char c = s[i];
		out += c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : std::string(1, c);
	}
	return out + (s.size() > n ? "..." : "");
}

inline std::string script(const std::string &s)
{
	return "len=" + std::to_string(s.size()) + " hash=" + hash(s) + " head=\"" + snippet(s) + "\"";
}

inline std::string redactTokens(const std::string &s)
{
	static const std::regex token(R"(((?:token|code|key|secret)[A-Za-z_]*=)[^&"'\s#]+)", std::regex::icase);
	return std::regex_replace(s, token, "$1<redacted>");
}
} // namespace SlDiag
