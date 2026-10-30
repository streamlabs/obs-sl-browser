#pragma once

// Module-internal.
// Transitions dock for the dual canvas, mirroring the main Scene Transitions dock:
// transition combo, duration, add/remove/properties for configurable types.

#include "SlDualController.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

class SlDualTransitionsDock : public QWidget
{
public:
	explicit SlDualTransitionsDock(SlDualController &controller);

	// Rebuilds the combo from the transitions model and syncs duration/buttons.
	void refresh();

private:
	void onSelectionChanged();
	void onAdd();
	void onRemove();
	void onProperties();

	SlDualController &m_controller;
	QComboBox *m_combo = nullptr;
	QLabel *m_durationLabel = nullptr;
	QSpinBox *m_duration = nullptr;
	QPushButton *m_addButton = nullptr;
	QPushButton *m_removeButton = nullptr;
	QPushButton *m_propertiesButton = nullptr;
	bool m_updating = false;
};
