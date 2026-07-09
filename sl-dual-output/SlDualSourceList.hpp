#pragma once

// Module-internal. Scene-item list for the dual canvas's active scene:
// selection synced with the preview, visibility checkboxes, drag reorder,
// and the same (undoable) item context menu as the preview editor.

#include "SlDualController.hpp"

#include <QListWidget>

class SlDualPreview;

class SlDualSourceList : public QListWidget
{
public:
	SlDualSourceList(SlDualController& controller, SlDualPreview *preview, QWidget *parent);
	~SlDualSourceList() override;

	// (Re)binds scene signals to the current active scene and rebuilds.
	void bindActiveScene();
	void rebuild();

protected:
	void contextMenuEvent(QContextMenuEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void unbindScene();
	void queueRebuild();
	static void sceneSignalThunk(void *data, calldata_t *cd);

	void onItemChanged(QListWidgetItem *listItem);
	void onSelectionChanged();
	obs_sceneitem_t *sceneItemForRow(int row) const; // borrowed

	SlDualController& m_controller;
	SlDualPreview *m_preview = nullptr;
	obs_source_t *m_boundSceneSource = nullptr; // ref'd while signals connected
	bool m_updating = false;
};
