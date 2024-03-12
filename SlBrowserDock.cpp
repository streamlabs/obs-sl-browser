#include "SlBrowserDock.h"

#include <QCloseEvent>

void SlBrowserDock::closeEvent(QCloseEvent *event)
{
	event->ignore();
	setHidden(true);
}

void SlBrowserDock::showEvent(QShowEvent *event)
{
	setHidden(false);
}
