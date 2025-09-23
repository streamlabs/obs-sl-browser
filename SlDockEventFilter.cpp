#include "SlDockEventFilter.h"

bool SlDockEventFilter::eventFilter(QObject *obj, QEvent *event) /*override*/
{
	if (event->type() == QEvent::Close)
	{
		event->ignore();
		m_parent->setHidden(true);
		return true;
	}
	else if (event->type() == QEvent::Show)
	{
		m_parent->setHidden(false);
	}

	return QObject::eventFilter(obj, event);
}
