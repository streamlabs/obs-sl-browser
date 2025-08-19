#pragma once

#include <QScopedPointer>
#include <QDockWidget>

class SlBrowserDock : public QDockWidget
{
public:
	inline SlBrowserDock(QWidget *parent = nullptr) : QDockWidget(parent) { setAttribute(Qt::WA_NativeWindow); }

	void closeEvent(QCloseEvent *event) override;
	void showEvent(QShowEvent *event) override;
};
