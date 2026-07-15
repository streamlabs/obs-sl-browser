#pragma once

// Module-internal.
// Sources dock for the dual canvas: the active scene's item list plus add/remove/properties/reorder controls.

#include "SlDualController.hpp"

#include <QWidget>

class QToolButton;
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
	QToolButton* m_addButton = nullptr;
	QToolButton* m_removeButton = nullptr;
	QToolButton* m_propertiesButton = nullptr;
	QToolButton* m_upButton = nullptr;
	QToolButton* m_downButton = nullptr;
};
