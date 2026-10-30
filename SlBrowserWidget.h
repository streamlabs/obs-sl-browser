#pragma once

#include <QWidget>
#include <QPaintEngine>

#include <memory>

struct BrowserElements;

class SlBrowserWidget : public QWidget
{
public:
	SlBrowserWidget();

	void setElements(std::weak_ptr<BrowserElements> elements) { m_elements = elements; }

protected:
	void closeEvent(QCloseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	QPaintEngine *paintEngine() const override;

private:
	std::weak_ptr<BrowserElements> m_elements;
};
