#include <Windows.h>

#include "SlDualDock.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualEditor.hpp"
#include "SlDualSettingsDialog.hpp"
#include "SlDualSourceList.hpp"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

// ---- SlDualPreview ----------------------------------------------------------

SlDualPreview::SlDualPreview(SlDualOutput::Impl &impl, QWidget *parent) : QWidget(parent), m_impl(impl)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	setFocusPolicy(Qt::ClickFocus);
	setMouseTracking(true); // hover cursors, like the main OBS preview
	setMinimumSize(120, 160);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	m_editor = std::make_unique<SlDualEditor>(impl, this);
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

	// OBS's preview background grey; the canvas area itself gets a black
	// backdrop in renderPreview so the two are distinguishable.
	m_display = obs_display_create(&info, 0xFF4C4C4C);
	updateCallbackRegistration();
}

void SlDualPreview::destroyDisplay()
{
	if (!m_display)
		return;

	if (m_callbackAdded) {
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

	if (want) {
		obs_display_add_draw_callback(m_display, drawThunk, this);
		m_callbackAdded = true;
	} else if (m_display) {
		// Synchronous: no draw callback is running once this returns.
		obs_display_remove_draw_callback(m_display, drawThunk, this);
		m_callbackAdded = false;
	}
}

void SlDualPreview::drawThunk(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<SlDualPreview *>(data);
	if (SlDualCanvas *canvas = self->m_impl.canvas.get())
		canvas->renderPreview(cx, cy);
	self->m_editor->drawOverlay(cx, cy);
}

void SlDualPreview::syncEditorView()
{
	m_editor->setViewSize(QSizeF(size()), devicePixelRatioF());
}

void SlDualPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	syncEditorView();
	createDisplay();
}

void SlDualPreview::hideEvent(QHideEvent *event)
{
	destroyDisplay();
	QWidget::hideEvent(event);
}

void SlDualPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	syncEditorView();
	if (m_display) {
		QSize scaled = size() * devicePixelRatioF();
		obs_display_resize(m_display, (uint32_t)scaled.width(), (uint32_t)scaled.height());
	}
}

void SlDualPreview::paintEvent(QPaintEvent *)
{
	// Rendering is done by libobs into the native window.
}

void SlDualPreview::mousePressEvent(QMouseEvent *event)
{
	syncEditorView();
	m_editor->mousePress(event->position(), event->button(), event->modifiers());
	QWidget::mousePressEvent(event);
}

void SlDualPreview::mouseMoveEvent(QMouseEvent *event)
{
	syncEditorView();
	m_editor->mouseMove(event->position(), (event->buttons() & Qt::LeftButton) != 0, event->modifiers());
	QWidget::mouseMoveEvent(event);
}

void SlDualPreview::mouseReleaseEvent(QMouseEvent *event)
{
	m_editor->mouseRelease(event->position(), event->button(), event->modifiers());
	QWidget::mouseReleaseEvent(event);
}

void SlDualPreview::leaveEvent(QEvent *event)
{
	m_editor->mouseLeave();
	QWidget::leaveEvent(event);
}

void SlDualPreview::resetEditor(bool clearUndo)
{
	m_editor->reset(clearUndo);
}

void SlDualPreview::mouseDoubleClickEvent(QMouseEvent *event)
{
	syncEditorView();
	m_editor->mouseDoubleClick(event->position());
	QWidget::mouseDoubleClickEvent(event);
}

void SlDualPreview::contextMenuEvent(QContextMenuEvent *event)
{
	syncEditorView();
	m_editor->contextMenu(QPointF(event->pos()), this);
	event->accept();
}

void SlDualPreview::keyPressEvent(QKeyEvent *event)
{
	if (m_editor->keyPress(event->key(), event->modifiers())) {
		event->accept();
		return;
	}
	QWidget::keyPressEvent(event);
}

// ---- SlDualDock -------------------------------------------------------------

SlDualDock::SlDualDock(SlDualOutput::Impl &impl) : QWidget(nullptr), m_impl(impl)
{
	m_preview = new SlDualPreview(impl, this);
	m_sourceList = new SlDualSourceList(impl, m_preview, this);

	m_sceneCombo = new QComboBox(this);
	m_sceneCombo->setToolTip("Active canvas scene");

	m_addSceneButton = new QToolButton(this);
	m_addSceneButton->setText("+");
	m_addSceneButton->setToolTip("Add scene");
	m_removeSceneButton = new QToolButton(this);
	m_removeSceneButton->setText("-");
	m_removeSceneButton->setToolTip("Remove scene");
	m_renameSceneButton = new QToolButton(this);
	m_renameSceneButton->setText("R");
	m_renameSceneButton->setToolTip("Rename scene");

	auto *sceneRow = new QHBoxLayout();
	sceneRow->setContentsMargins(0, 0, 0, 0);
	sceneRow->addWidget(new QLabel("Scene:", this));
	sceneRow->addWidget(m_sceneCombo, 1);
	sceneRow->addWidget(m_addSceneButton);
	sceneRow->addWidget(m_removeSceneButton);
	sceneRow->addWidget(m_renameSceneButton);

	m_settingsButton = new QPushButton("Settings", this);
	m_startStopButton = new QPushButton("Start", this);
	m_statusLabel = new QLabel(this);
	m_statusLabel->setTextFormat(Qt::RichText);

	auto *controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->addWidget(m_statusLabel, 1);
	controls->addWidget(m_settingsButton);
	controls->addWidget(m_startStopButton);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(m_preview, 1);
	layout->addLayout(sceneRow);
	layout->addWidget(m_sourceList);
	layout->addLayout(controls);

	QObject::connect(m_sceneCombo, &QComboBox::currentIndexChanged, this,
			 [this](int index) { onSceneComboChanged(index); });
	QObject::connect(m_addSceneButton, &QToolButton::clicked, this, [this]() { onAddScene(); });
	QObject::connect(m_removeSceneButton, &QToolButton::clicked, this, [this]() { onRemoveScene(); });
	QObject::connect(m_renameSceneButton, &QToolButton::clicked, this, [this]() { onRenameScene(); });
	QObject::connect(m_settingsButton, &QPushButton::clicked, this, [this]() { openSettings(); });
	QObject::connect(m_startStopButton, &QPushButton::clicked, this, [this]() { onStartStopClicked(); });

	refreshScenes();
	setStreamState(SlDualStreamState::Idle, std::string());
}

void SlDualDock::refreshScenes()
{
	m_updatingCombo = true;
	m_sceneCombo->clear();

	SlDualCanvas *canvas = m_impl.canvas.get();
	if (canvas && canvas->valid()) {
		std::string active = canvas->activeSceneName();
		int activeIndex = 0;
		int i = 0;
		for (const std::string &name : canvas->sceneNames()) {
			m_sceneCombo->addItem(QString::fromUtf8(name.c_str()));
			if (name == active)
				activeIndex = i;
			i++;
		}
		m_sceneCombo->setCurrentIndex(activeIndex);
	}

	bool haveScenes = m_sceneCombo->count() > 0;
	m_sceneCombo->setEnabled(haveScenes);
	m_removeSceneButton->setEnabled(m_sceneCombo->count() > 1);
	m_renameSceneButton->setEnabled(haveScenes);
	m_addSceneButton->setEnabled(canvas && canvas->valid());

	m_updatingCombo = false;

	if (m_sourceList)
		m_sourceList->bindActiveScene();
}

void SlDualDock::onSceneComboChanged(int index)
{
	if (m_updatingCombo || index < 0)
		return;

	m_impl.sceneSetActive(m_sceneCombo->itemText(index).toUtf8().constData());
}

void SlDualDock::onAddScene()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, "Add Scene", "Scene name:", QLineEdit::Normal, QString(), &ok);
	name = name.trimmed();
	if (!ok || name.isEmpty())
		return;

	if (!m_impl.sceneCreate(name.toUtf8().constData()))
		QMessageBox::information(this, "Add Scene", "A scene with that name already exists on this canvas.");
}

void SlDualDock::onRemoveScene()
{
	SlDualCanvas *canvas = m_impl.canvas.get();
	if (!canvas)
		return;

	QString name = QString::fromUtf8(canvas->activeSceneName().c_str());
	if (QMessageBox::question(this, "Remove Scene", QString("Remove scene '%1' and its items?").arg(name)) !=
	    QMessageBox::Yes)
		return;

	m_impl.sceneRemoveActive();
}

void SlDualDock::onRenameScene()
{
	SlDualCanvas *canvas = m_impl.canvas.get();
	if (!canvas)
		return;

	bool ok = false;
	QString name = QInputDialog::getText(this, "Rename Scene", "Scene name:", QLineEdit::Normal,
					     QString::fromUtf8(canvas->activeSceneName().c_str()), &ok);
	name = name.trimmed();
	if (!ok || name.isEmpty())
		return;

	if (!m_impl.sceneRenameActive(name.toUtf8().constData()))
		QMessageBox::information(this, "Rename Scene", "A scene with that name already exists on this canvas.");
}

void SlDualDock::onStartStopClicked()
{
	switch (m_state) {
	case SlDualStreamState::Idle:
		if (m_impl.config.server.empty()) {
			openSettings();
			return;
		}
		m_impl.startStream();
		break;
	case SlDualStreamState::Starting:
	case SlDualStreamState::Live:
	case SlDualStreamState::Reconnecting:
		m_impl.stopStream();
		break;
	case SlDualStreamState::Stopping:
		break;
	}
}

void SlDualDock::openSettings()
{
	SlDualSettingsDialog dialog(m_impl.config, m_impl.streamActive(), this);
	if (dialog.exec() == QDialog::Accepted)
		m_impl.applySettings(dialog.resultConfig());
}

void SlDualDock::setStreamState(SlDualStreamState state, const std::string &msg)
{
	m_state = state;

	const char *text = "Idle";
	const char *color = "#909090";
	switch (state) {
	case SlDualStreamState::Starting:
		text = "Connecting";
		color = "#e0a800";
		break;
	case SlDualStreamState::Live:
		text = "Live";
		color = "#2ecc71";
		break;
	case SlDualStreamState::Reconnecting:
		text = "Reconnecting";
		color = "#e0a800";
		break;
	case SlDualStreamState::Stopping:
		text = "Stopping";
		color = "#909090";
		break;
	case SlDualStreamState::Idle:
		break;
	}

	QString status = QString("<span style=\"color:%1\">%2</span> %3")
				 .arg(QString::fromUtf8(color))
				 .arg(QChar(0x25CF))
				 .arg(QString::fromUtf8(text));
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
