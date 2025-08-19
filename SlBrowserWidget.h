#pragma once

#include <QWidget>
#include <QPaintEngine>

class SlBrowserWidget : public QWidget
{
public:
	SlBrowserWidget();

protected:
	void closeEvent(QCloseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

	void showEvent(QShowEvent *event) override;
	QPaintEngine *paintEngine() const override;
};
