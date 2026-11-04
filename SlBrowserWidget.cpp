#include "SlBrowserWidget.h"
#include "SlBrowser.h"
#include "WindowsFunctions.h"

#include <QDialog>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>

SlBrowserWidget::SlBrowserWidget()
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	setFocusPolicy(Qt::ClickFocus);

	if (WindowsFunctions::TestOffscreen())
	{
		setAttribute(Qt::WA_ShowWithoutActivating);
		setWindowFlag(Qt::WindowDoesNotAcceptFocus);
		move(-32000, -32000);
	}

	setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/streamlabs-app-icon.png"));
}

void SlBrowserWidget::closeEvent(QCloseEvent *event) /*override*/
{
	event->ignore();

	auto elements = m_elements.lock();
	if (!elements)
		return;

	if (elements != SlBrowser::instance().m_mainBrowser)
	{
		if (elements->hideOnClose)
			setHidden(true);
		else
			SlBrowser::instance().closeTabWindow(elements->uid, elements);

		return;
	}

	if (SlBrowser::instance().m_allowHideBrowser)
		setHidden(true);

	SlBrowser::instance().saveHiddenState(isHidden());
}

void SlBrowserWidget::resizeEvent(QResizeEvent *event) /*override*/
{
	QWidget::resizeEvent(event);
	scheduleBrowserSizeSync();
}

void SlBrowserWidget::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::WindowStateChange)
		scheduleBrowserSizeSync();
}

void SlBrowserWidget::scheduleBrowserSizeSync()
{
	if (m_browserSizeSyncScheduled)
		return;

	m_browserSizeSyncScheduled = true;
	QTimer::singleShot(0, this, [this]() {
		m_browserSizeSyncScheduled = false;
		syncBrowserSize();
	});
}

void SlBrowserWidget::syncBrowserSize()
{
	auto elements = m_elements.lock();
	if (!elements || elements->closed)
		return;

	RECT clientRect = {};
	if (!::GetClientRect((HWND)winId(), &clientRect))
		return;

	const int width = clientRect.right - clientRect.left;
	const int height = clientRect.bottom - clientRect.top;
	if (width <= 0 || height <= 0)
		return;

	class BrowserTask : public CefTask
	{
	public:
		std::function<void()> task;
		inline BrowserTask(std::function<void()> task_) : task(task_) {}
		void Execute() override { task(); }
		IMPLEMENT_REFCOUNTING(BrowserTask);
	};

	auto resizeBrowser = [elements, width, height]() {
		if (elements->closed || !elements->browser)
			return;

		CefWindowHandle handle = elements->browser->GetHost()->GetWindowHandle();
		if (!handle)
			return;

		::SetWindowPos((HWND)handle, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		::SendMessage((HWND)handle, WM_SIZE, 0, MAKELPARAM(width, height));
	};
	CefPostTask(TID_UI, CefRefPtr<BrowserTask>(new BrowserTask(resizeBrowser)));
}

void SlBrowserWidget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);

	if (auto elements = m_elements.lock())
	{
		std::lock_guard<std::mutex> lock(elements->hiddenStateMutex);
		if (!elements->pendingHiddenOperations)
			elements->hidden = isHidden();
	}
	scheduleBrowserSizeSync();
}

void SlBrowserWidget::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);

	if (auto elements = m_elements.lock())
	{
		std::lock_guard<std::mutex> lock(elements->hiddenStateMutex);
		if (!elements->pendingHiddenOperations)
			elements->hidden = isHidden();
	}
}

QPaintEngine *SlBrowserWidget::paintEngine() const
{
	return nullptr;
}
