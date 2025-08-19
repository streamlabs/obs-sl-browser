#pragma once

#include "cef-headers.hpp"
#include <string>
#include <fstream>

#if CHROME_VERSION_BUILD < 4638
#define ENABLE_LOCAL_FILE_URL_SCHEME 1
#else
#define ENABLE_LOCAL_FILE_URL_SCHEME 0
#endif

#if !ENABLE_LOCAL_FILE_URL_SCHEME
class BrowserSchemeHandlerFactory : public CefSchemeHandlerFactory
{
public:
	virtual CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, const CefString &, CefRefPtr<CefRequest> request) override;

	IMPLEMENT_REFCOUNTING(BrowserSchemeHandlerFactory);
};
#endif
