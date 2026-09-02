#pragma once

// Module-internal.
// Sources dock for the dual canvas: the active scene's item list plus add/remove/properties/reorder controls.

#include "SlDualController.hpp"

#include <QWidget>

class QAction;
class QToolBar;
class SlDualPreview;
class SlDualSourceList;

class SlDualSourcesDock : public QWidget
{
public:
	SlDualSourcesDock(SlDualController& controller, SlDualPreview* preview);

	// (Re)binds the list to the current active scene.
	void refreshBinding();

private:
	void onAdd();
	void updateButtons();

	SlDualController& m_controller;
	SlDualPreview* m_preview = nullptr;
	SlDualSourceList* m_list = nullptr;
	QToolBar* m_toolbar = nullptr;
	QAction* m_addAction = nullptr;
	QAction* m_removeAction = nullptr;
	QAction* m_propertiesAction = nullptr;
	QAction* m_upAction = nullptr;
	QAction* m_downAction = nullptr;
};
