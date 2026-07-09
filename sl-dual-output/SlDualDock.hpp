#pragma once

// Module-internal.
// Dock content widget: interactive canvas preview, scene management row, settings, start/stop.

#include "SlDualController.hpp"
#include "SlDualStreamOutput.hpp"

#include <QWidget>

#include <memory>
#include <string>

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class SlDualEditor;
class SlDualSourceList;

class SlDualPreview : public QWidget
{
public:
	explicit SlDualPreview(SlDualController& controller, QWidget* parent = nullptr);
	~SlDualPreview() override;

	// While inactive the draw callback is removed; the facade deactivates around canvas detach/reattach so the graphics thread never touches a canvas that is being swapped out.
	void setActive(bool active);

	// Drops drag/hover state; clearUndo also drops undo history.
	void resetEditor(bool clearUndo);

	SlDualEditor& editor() { return *m_editor; }

	QPaintEngine* paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void createDisplay();
	void destroyDisplay();
	void updateCallbackRegistration();
	void syncEditorView();
	static void drawThunk(void* data, uint32_t cx, uint32_t cy);

	SlDualController& m_controller;
	std::unique_ptr<SlDualEditor> m_editor;
	obs_display_t* m_display = nullptr;
	bool m_active = true;
	bool m_callbackAdded = false;
};

class SlDualDock : public QWidget
{
public:
	explicit SlDualDock(SlDualController& controller);

	void refreshScenes();
	void setStreamState(SlDualStreamState state, const std::string& msg);
	void setPreviewActive(bool active);
	void resetEditorState(bool clearUndo);

private:
	void onSceneComboChanged(int index);
	void onAddScene();
	void onRemoveScene();
	void onRenameScene();
	void onStartStopClicked();
	void openSettings();

	SlDualController& m_controller;
	SlDualPreview* m_preview = nullptr;
	SlDualSourceList* m_sourceList = nullptr;
	QComboBox* m_sceneCombo = nullptr;
	QToolButton* m_addSceneButton = nullptr;
	QToolButton* m_removeSceneButton = nullptr;
	QToolButton* m_renameSceneButton = nullptr;
	QLabel* m_statusLabel = nullptr;
	QPushButton* m_settingsButton = nullptr;
	QPushButton* m_startStopButton = nullptr;
	SlDualStreamState m_state = SlDualStreamState::Idle;
	bool m_updatingCombo = false;
};
