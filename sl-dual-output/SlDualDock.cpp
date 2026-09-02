#include <Windows.h>

#include "SlDualDock.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualEditor.hpp"
#include "SlDualSettingsDialog.hpp"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

/**
* SlDualPreview
*/

SlDualPreview::SlDualPreview(SlDualController& controller, QWidget* parent) : QWidget(parent), m_controller(controller)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	setFocusPolicy(Qt::ClickFocus);

	// hover cursors, like the main OBS preview
	setMouseTracking(true);
	setMinimumSize(120, 160);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	m_editor = std::make_unique<SlDualEditor>(controller, this);
}

SlDualPreview::~SlDualPreview()
{
	destroyDisplay();
}

void SlDualPreview::setActive(bool active)
{
	m_active = active;
	updateCallbackRegistration();
}

void SlDualPreview::createDisplay()
{
	if (m_display || !isVisible())
		return;

	QSize scaled = size() * devicePixelRatioF();

	gs_init_data info = {};
	info.cx = (uint32_t)scaled.width();
	info.cy = (uint32_t)scaled.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = (HWND)winId();

	// Default bg color
	uint32_t bgColor = 0xFF1A1413;
	auto* main = static_cast<QWidget*>(obs_frontend_get_main_window());
	QWidget* mainPreview = main ? main->findChild<QWidget*>("preview") : nullptr;
	QVariant themed = mainPreview ? mainPreview->property("displayBackgroundColor") : QVariant();

	if (themed.isValid() && themed.value<QColor>().isValid())
	{
		QColor bg = themed.value<QColor>();
		bgColor = ((uint32_t)bg.alpha() << 24) | ((uint32_t)bg.blue() << 16) | ((uint32_t)bg.green() << 8) | (uint32_t)bg.red();
	}

	m_display = obs_display_create(&info, bgColor);
	updateCallbackRegistration();
}

void SlDualPreview::destroyDisplay()
{
	if (!m_display)
		return;

	if (m_callbackAdded)
	{
		obs_display_remove_draw_callback(m_display, drawThunk, this);
		m_callbackAdded = false;
	}

	obs_display_destroy(m_display);
	m_display = nullptr;
}

void SlDualPreview::updateCallbackRegistration()
{
	bool want = m_display && m_active;

	if (want == m_callbackAdded)
		return;

	if (want)
	{
		obs_display_add_draw_callback(m_display, drawThunk, this);
		m_callbackAdded = true;
	}
	else if (m_display)
	{
		// Synchronous: no draw callback is running once this returns.
		obs_display_remove_draw_callback(m_display, drawThunk, this);
		m_callbackAdded = false;
	}
}

void SlDualPreview::drawThunk(void* data, uint32_t cx, uint32_t cy)
{
	auto* self = static_cast<SlDualPreview*>(data);

	if (SlDualCanvas* canvas = self->m_controller.canvas.get())
		canvas->renderPreview(cx, cy);
	self->m_editor->drawOverlay(cx, cy);
}

void SlDualPreview::syncEditorView()
{
	m_editor->setViewSize(QSizeF(size()), devicePixelRatioF());
}

void SlDualPreview::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	syncEditorView();
	createDisplay();
}

void SlDualPreview::hideEvent(QHideEvent* event)
{
	destroyDisplay();
	QWidget::hideEvent(event);
}

void SlDualPreview::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	syncEditorView();

	if (m_display)
	{
		QSize scaled = size() * devicePixelRatioF();
		obs_display_resize(m_display, (uint32_t)scaled.width(), (uint32_t)scaled.height());
	}
}

void SlDualPreview::paintEvent(QPaintEvent*)
{
	// Rendering is done by libobs into the native window.
}

void SlDualPreview::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);

	if ((event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) && m_display)
	{
		// Recreate so the letterbox picks up the new theme color.
		destroyDisplay();
		createDisplay();
	}
}

void SlDualPreview::mousePressEvent(QMouseEvent* event)
{
	syncEditorView();
	m_editor->mousePress(event->position(), event->button(), event->modifiers());
	QWidget::mousePressEvent(event);
}

void SlDualPreview::mouseMoveEvent(QMouseEvent* event)
{
	syncEditorView();
	m_editor->mouseMove(event->position(), (event->buttons() & Qt::LeftButton) != 0, event->modifiers());
	QWidget::mouseMoveEvent(event);
}

void SlDualPreview::mouseReleaseEvent(QMouseEvent* event)
{
	m_editor->mouseRelease(event->position(), event->button(), event->modifiers());
	QWidget::mouseReleaseEvent(event);
}

void SlDualPreview::leaveEvent(QEvent* event)
{
	m_editor->mouseLeave();
	QWidget::leaveEvent(event);
}

void SlDualPreview::resetEditor(bool clearUndo)
{
	m_editor->reset(clearUndo);
}

void SlDualPreview::mouseDoubleClickEvent(QMouseEvent* event)
{
	syncEditorView();
	m_editor->mouseDoubleClick(event->position());
	QWidget::mouseDoubleClickEvent(event);
}

void SlDualPreview::contextMenuEvent(QContextMenuEvent* event)
{
	syncEditorView();
	m_editor->contextMenu(QPointF(event->pos()), this);
	event->accept();
}

void SlDualPreview::keyPressEvent(QKeyEvent* event)
{
	if (m_editor->keyPress(event->key(), event->modifiers()))
	{
		event->accept();
		return;
	}

	QWidget::keyPressEvent(event);
}

/**
* SlDualDock
*/

SlDualDock::SlDualDock(SlDualController& controller) : QWidget(nullptr), m_controller(controller)
{
	m_preview = new SlDualPreview(controller, this);

	m_settingsButton = new QPushButton("Settings", this);
	m_startStopButton = new QPushButton("Start", this);
	m_statusLabel = new QLabel(this);
	m_statusLabel->setTextFormat(Qt::RichText);

	auto* controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->addWidget(m_statusLabel, 1);
	controls->addWidget(m_settingsButton);
	controls->addWidget(m_startStopButton);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(m_preview, 1);
	layout->addLayout(controls);

	QObject::connect(m_settingsButton, &QPushButton::clicked, this, [this]() { openSettings(); });
	QObject::connect(m_startStopButton, &QPushButton::clicked, this, [this]() { onStartStopClicked(); });

	setStreamState(SlDualStreamState::Idle, std::string());
}

void SlDualDock::onStartStopClicked()
{
	switch (m_state)
	{
	case SlDualStreamState::Idle:
	{
		if (m_controller.config.server.empty())
		{
			openSettings();
			return;
		}

		m_controller.startStream();
		break;
	}
	case SlDualStreamState::Starting:
	case SlDualStreamState::Live:
	case SlDualStreamState::Reconnecting:
	{
		m_controller.stopStream();
		break;
	}
	case SlDualStreamState::Stopping:
	{
		break;
	}
	}
}

void SlDualDock::openSettings()
{
	SlDualSettingsDialog dialog(m_controller.config, m_controller.streamBusy(), this);

	if (dialog.exec() == QDialog::Accepted)
		m_controller.applySettings(dialog.resultConfig());
}

void SlDualDock::setStreamState(SlDualStreamState state, const std::string& msg)
{
	m_state = state;

	const char* text = "Idle";
	const char* color = "#909090";
	switch (state)
	{
	case SlDualStreamState::Starting:
	{
		text = "Connecting";
		color = "#e0a800";
		break;
	}
	case SlDualStreamState::Live:
	{
		text = "Live";
		color = "#2ecc71";
		break;
	}
	case SlDualStreamState::Reconnecting:
	{
		text = "Reconnecting";
		color = "#e0a800";
		break;
	}
	case SlDualStreamState::Stopping:
	{
		text = "Stopping";
		color = "#909090";
		break;
	}
	case SlDualStreamState::Idle:
	{
		break;
	}
	}

	QString status = QString("<span style=\"color:%1\">%2</span> %3").arg(QString::fromUtf8(color)).arg(QChar(0x25CF)).arg(QString::fromUtf8(text));

	if (!msg.empty() && msg != text)
		status += QString(" - %1").arg(QString::fromUtf8(msg.c_str()).toHtmlEscaped());

	m_statusLabel->setText(status);
	m_startStopButton->setText(state == SlDualStreamState::Idle ? "Start" : "Stop");
	m_startStopButton->setEnabled(state != SlDualStreamState::Stopping);
}

void SlDualDock::setPreviewActive(bool active)
{
	if (m_preview)
		m_preview->setActive(active);
}

void SlDualDock::resetEditorState(bool clearUndo)
{
	if (m_preview)
		m_preview->resetEditor(clearUndo);
}
