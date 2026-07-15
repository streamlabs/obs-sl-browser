#pragma once

// Module-internal.
// Scenes dock for the dual canvas: standalone list of canvas scenes, selection drives the active scene.
// Add/remove/rename plus user-defined display order (drag or buttons), persisted with the scene collection.

#include "SlDualController.hpp"

#include <QWidget>

#include <string>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QToolButton;

class SlDualScenesDock : public QWidget
{
public:
	explicit SlDualScenesDock(SlDualController& controller);

	// Rebuilds the list from the canvas and config.sceneOrder; highlights the active scene.
	void refresh();

private:
	void onSelectionChanged();
	void onItemEdited(QListWidgetItem* item);
	void onAdd();
	void onRemove();
	void onMove(int direction);
	void onOrderDropped();
	void showContextMenu(const QPoint& pos);

	std::vector<std::string> displayedNames() const;
	void persistOrder();

	SlDualController& m_controller;
	QListWidget* m_list = nullptr;
	QToolButton* m_addButton = nullptr;
	QToolButton* m_removeButton = nullptr;
	QToolButton* m_upButton = nullptr;
	QToolButton* m_downButton = nullptr;
	bool m_updating = false;
};
