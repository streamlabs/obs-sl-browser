#include "SlBrowserWidget.h"
#include "SlBrowser.h"

#include <QDialog>
#include <QCloseEvent>
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

	wchar_t buffer[MAX_PATH];
	DWORD size = GetCurrentDirectory(MAX_PATH, buffer);
	std::wstring iconpath = std::wstring(buffer) + L"/../../obs-plugins/64bit/streamlabs-app-icon.png";
	setWindowIcon(QIcon(QString::fromStdWString(iconpath)));
}

void SlBrowserWidget::closeEvent(QCloseEvent *event) /*override*/
{
	event->ignore();

	if (SlBrowser::instance().m_allowHideBrowser)
		setHidden(true);
}

void SlBrowserWidget::resizeEvent(QResizeEvent *event) /*override*/
{
	QWidget::resizeEvent(event);

	if (SlBrowser::instance().m_browser != nullptr)
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

		QueueCEFTask([this, size]() {
			CefWindowHandle handle = SlBrowser::instance().m_browser->GetHost()->GetWindowHandle();

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
}

QPaintEngine *SlBrowserWidget::paintEngine() const
{
	return nullptr;
}
