#pragma once

// Module-internal.
// Preview dock content: interactive canvas preview with stream status, settings and start/stop.
// Scene and source management live in their own docks (SlDualScenesDock / SlDualSourcesDock).

#include "SlDualController.hpp"
#include "SlDualStreamOutput.hpp"

#include <QWidget>

#include <memory>
#include <string>

class QLabel;
class QPushButton;
class SlDualEditor;

class SlDualPreview : public QWidget
{
public:
	explicit SlDualPreview(SlDualController &controller, QWidget *parent = nullptr);
	~SlDualPreview() override;

	// While inactive the draw callback is removed; the facade deactivates around canvas detach/reattach so the graphics thread never touches a canvas that is being swapped out.
	void setActive(bool active);

	// Drops drag/hover state; clearUndo also drops undo history.
	void resetEditor(bool clearUndo);

	SlDualEditor &editor() { return *m_editor; }

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	void createDisplay();
	void destroyDisplay();
	void updateCallbackRegistration();
	void syncEditorView();
	static void drawThunk(void *data, uint32_t cx, uint32_t cy);

	SlDualController &m_controller;
	std::unique_ptr<SlDualEditor> m_editor;
	obs_display_t *m_display = nullptr;
	bool m_active = true;
	bool m_callbackAdded = false;
};

class SlDualDock : public QWidget
{
public:
	explicit SlDualDock(SlDualController &controller);

	void setStreamState(SlDualStreamState state, const std::string &msg);
	void setPreviewActive(bool active);
	void resetEditorState(bool clearUndo);

	SlDualPreview *preview() const { return m_preview; }

private:
	void onStartStopClicked();
	void openSettings();

	SlDualController &m_controller;
	SlDualPreview *m_preview = nullptr;
	QLabel *m_statusLabel = nullptr;
	QPushButton *m_settingsButton = nullptr;
	QPushButton *m_startStopButton = nullptr;
	SlDualStreamState m_state = SlDualStreamState::Idle;
};
