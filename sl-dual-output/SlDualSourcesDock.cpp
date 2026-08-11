#include "SlDualSourcesDock.hpp"
#include "SlDualDock.hpp"
#include "SlDualEditor.hpp"
#include "SlDualSourceList.hpp"
#include "SlDualToolbar.hpp"

#include <QAbstractItemModel>
#include <QCursor>
#include <QVBoxLayout>

SlDualSourcesDock::SlDualSourcesDock(SlDualController& controller, SlDualPreview* preview)
	: QWidget(nullptr),
	  m_controller(controller),
	  m_preview(preview)
{
	m_list = new SlDualSourceList(controller, preview, this);

	// Same construction, ordering and separators as OBS's sourcesToolbar (minus filters): OBS's own icons, theme classes, OBSDock chrome.
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(16, 16));
	m_toolbar->setFloatable(false);
	m_toolbar->setMovable(false);

	m_addAction = slDualToolAction(m_toolbar, ":/res/images/plus.svg", "icon-plus", "Add", "Add source");
	m_removeAction = slDualToolAction(m_toolbar, ":/res/images/minus.svg", "icon-trash", "Remove", "Remove selected sources");
	m_toolbar->addSeparator();
	m_propertiesAction = slDualToolAction(m_toolbar, ":/settings/images/settings/general.svg", "icon-gear", "Properties", "Source properties");
	m_toolbar->addSeparator();
	m_upAction = slDualToolAction(m_toolbar, ":/res/images/up.svg", "icon-up", "Move Up", "Move source up");
	m_downAction = slDualToolAction(m_toolbar, ":/res/images/down.svg", "icon-down", "Move Down", "Move source down");
	slDualApplyThemeProperties(m_toolbar);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_list, 1);
	layout->addWidget(m_toolbar);

	QObject::connect(m_addAction, &QAction::triggered, this, [this]() { onAdd(); });
	QObject::connect(m_removeAction, &QAction::triggered, this, [this]() { m_list->removeSelected(); });
	QObject::connect(m_propertiesAction, &QAction::triggered, this, [this]() { m_list->openSelectedProperties(); });
	QObject::connect(m_upAction, &QAction::triggered, this, [this]() { m_list->moveSelected(-1); });
	QObject::connect(m_downAction, &QAction::triggered, this, [this]() { m_list->moveSelected(1); });
	QObject::connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
	QObject::connect(m_list->model(), &QAbstractItemModel::rowsInserted, this, [this]() { updateButtons(); });
	QObject::connect(m_list->model(), &QAbstractItemModel::rowsRemoved, this, [this]() { updateButtons(); });

	refreshBinding();
}

void SlDualSourcesDock::refreshBinding()
{
	m_list->bindActiveScene();
	updateButtons();
}

void SlDualSourcesDock::onAdd()
{
	if (!m_preview)
		return;

	QWidget* button = m_toolbar->widgetForAction(m_addAction);
	QPoint pos = button ? button->mapToGlobal(QPoint(0, button->height())) : QCursor::pos();
	m_preview->editor().showAddSourceMenu(pos, this);
}

void SlDualSourcesDock::updateButtons()
{
	// The list disables itself when no scene is bound.
	bool haveScene = m_list->isEnabled();
	bool haveSelection = !m_list->selectedItems().isEmpty();

	m_addAction->setEnabled(haveScene);
	m_removeAction->setEnabled(haveScene && haveSelection);
	m_propertiesAction->setEnabled(haveScene && haveSelection);
	m_upAction->setEnabled(haveScene && haveSelection && m_list->count() > 1);
	m_downAction->setEnabled(haveScene && haveSelection && m_list->count() > 1);
}
