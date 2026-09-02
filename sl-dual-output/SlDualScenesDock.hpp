#pragma once

// Module-internal.
// Scenes dock for the dual canvas: standalone list of canvas scenes, selection drives the active scene.
// Add/remove/rename plus user-defined display order (drag or buttons), persisted with the scene collection.

#include "SlDualController.hpp"

#include <QWidget>

#include <string>
#include <vector>

class QAction;
class QListWidget;
class QListWidgetItem;
class QToolBar;

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
	QToolBar* m_toolbar = nullptr;
	QAction* m_addAction = nullptr;
	QAction* m_removeAction = nullptr;
	QAction* m_upAction = nullptr;
	QAction* m_downAction = nullptr;
	bool m_updating = false;
};
