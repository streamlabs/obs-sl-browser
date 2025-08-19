#include "SlBrowser.h"
#include "CrashHandler.h"

int main(int argc, char *argv[])
{
	CrashHandler::instance().setErrorTitle("Streamlabs Plugin");
	CrashHandler::instance().setErrorMsg("The browser for Streamlabs has stopped working. You will need to restart OBS to continue using the plugin correctly.");
	CrashHandler::instance().setUploadTitle("Streamlabs Plugin");
	CrashHandler::instance().setUploadMsg("Would you like to upload a detailed report for further review?");
	CrashHandler::instance().setSentryUri("https://o114354.ingest.sentry.io/api/4506154582736896/minidump/?sentry_key=a8512dae7a714f9710cc1957c1d1fcae");

	SlBrowser::instance().run(argc, argv);
	return true;
}
