#include "SlBrowserWidget.h"
#include "SlBrowser.h"
#include "WindowsFunctions.h"

#include <QDialog>
#include <QCloseEvent>
#include <QCoreApplication>
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

	if (auto elements = m_elements.lock())
	{
		QSize size = this->size() * devicePixelRatioF();

		class BrowserTask : public CefTask
		{
		public:
			std::function<void()> task;
			inline BrowserTask(std::function<void()> task_) : task(task_) {}
			void Execute() override { task(); }
			IMPLEMENT_REFCOUNTING(BrowserTask);
		};

		auto QueueCEFTask = [](std::function<void()> task) { return CefPostTask(TID_UI, CefRefPtr<BrowserTask>(new BrowserTask(task))); };

		QueueCEFTask([elements, size]() {
			if (!elements->browser)
				return;

			CefWindowHandle handle = elements->browser->GetHost()->GetWindowHandle();

			if (!handle)
				return;

			::SetWindowPos((HWND)handle, nullptr, 0, 0, size.width(), size.height(), SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
			::SendMessage((HWND)handle, WM_SIZE, 0, MAKELPARAM(size.width(), size.height()));
		});
	}
}

void SlBrowserWidget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);

	if (auto elements = m_elements.lock())
		elements->hidden = isHidden();
}

void SlBrowserWidget::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);

	if (auto elements = m_elements.lock())
		elements->hidden = isHidden();
}

QPaintEngine *SlBrowserWidget::paintEngine() const
{
	return nullptr;
}
