#include "SlBrowser.h"
#include "MakeMinidump.h"

int main(int argc, char *argv[])
{
	::SetUnhandledExceptionFilter(Util::unhandledHandler);
	SlBrowser::instance().run(argc, argv);
	return true;
}
