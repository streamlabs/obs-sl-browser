#pragma once

#include <QDockWidget>
#include <QEvent>

class SlDockEventFilter : public QObject
{
public:
	explicit SlDockEventFilter(QDockWidget *parent = nullptr) :
		QObject(parent),
		m_parent(parent)
	{}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

	QDockWidget *m_parent;
};
