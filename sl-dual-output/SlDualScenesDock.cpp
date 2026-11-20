#include "SlDualScenesDock.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualToolbar.hpp"

#include <QAbstractItemModel>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>

SlDualScenesDock::SlDualScenesDock(SlDualController &controller) : QWidget(nullptr), m_controller(controller)
{
	m_list = new QListWidget(this);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setDragDropMode(QAbstractItemView::InternalMove);
	m_list->setDefaultDropAction(Qt::MoveAction);
	m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_list->setContextMenuPolicy(Qt::CustomContextMenu);

	// Same construction as the main dock toolbars: OBS's own icons, theme classes and OBSDock toolbar chrome.
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(16, 16));
	m_toolbar->setFloatable(false);
	m_toolbar->setMovable(false);

	m_addAction = slDualToolAction(m_toolbar, ":/res/images/plus.svg", "icon-plus", "Add", "Add scene");
	m_removeAction = slDualToolAction(m_toolbar, ":/res/images/minus.svg", "icon-trash", "Remove", "Remove scene");
	m_toolbar->addSeparator();
	m_upAction = slDualToolAction(m_toolbar, ":/res/images/up.svg", "icon-up", "Move Up", "Move scene up");
	m_downAction = slDualToolAction(m_toolbar, ":/res/images/down.svg", "icon-down", "Move Down", "Move scene down");
	slDualApplyThemeProperties(m_toolbar);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_list, 1);
	layout->addWidget(m_toolbar);

	QObject::connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { onSelectionChanged(); });
	QObject::connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) { onItemEdited(item); });
	QObject::connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showContextMenu(pos); });
	QObject::connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]() { onOrderDropped(); });
	QObject::connect(m_addAction, &QAction::triggered, this, [this]() { onAdd(); });
	QObject::connect(m_removeAction, &QAction::triggered, this, [this]() { onRemove(); });
	QObject::connect(m_upAction, &QAction::triggered, this, [this]() { onMove(-1); });
	QObject::connect(m_downAction, &QAction::triggered, this, [this]() { onMove(1); });

	refresh();
}

void SlDualScenesDock::refresh()
{
	m_updating = true;
	m_list->clear();

	SlDualCanvas *canvas = m_controller.canvas.get();
	bool haveCanvas = canvas && canvas->valid();

	if (haveCanvas)
	{
		// config.sceneOrder decides display order; unknown scenes append, stale names drop.
		std::vector<std::string> names = canvas->sceneNames();
		std::vector<std::string> ordered;

		for (const std::string &name : m_controller.config.sceneOrder)
		{
			bool known = std::find(names.begin(), names.end(), name) != names.end();
			bool seen = std::find(ordered.begin(), ordered.end(), name) != ordered.end();

			if (known && !seen)
				ordered.push_back(name);
		}

		for (const std::string &name : names)
		{
			if (std::find(ordered.begin(), ordered.end(), name) == ordered.end())
				ordered.push_back(name);
		}

		m_controller.config.sceneOrder = ordered;
		std::string active = canvas->activeSceneName();

		for (const std::string &name : ordered)
		{
			auto *item = new QListWidgetItem(QString::fromUtf8(name.c_str()), m_list);
			item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

			// original name, for rename validation
			item->setData(Qt::UserRole, QString::fromUtf8(name.c_str()));

			if (name == active)
				m_list->setCurrentItem(item);
		}
	}

	m_list->setEnabled(haveCanvas);
	m_addAction->setEnabled(haveCanvas);
	m_removeAction->setEnabled(m_list->count() > 1);
	m_upAction->setEnabled(m_list->count() > 1);
	m_downAction->setEnabled(m_list->count() > 1);
	m_updating = false;
}

void SlDualScenesDock::onSelectionChanged()
{
	if (m_updating)
		return;

	QListWidgetItem *item = m_list->currentItem();

	if (!item)
		return;

	std::string name = item->text().toUtf8().constData();
	SlDualCanvas *canvas = m_controller.canvas.get();

	if (canvas && name != canvas->activeSceneName())
		m_controller.sceneSetActive(name);
}

void SlDualScenesDock::onItemEdited(QListWidgetItem *item)
{
	if (m_updating || !item)
		return;

	// Editing follows selection, so the edited row is the active scene.
	QString oldName = item->data(Qt::UserRole).toString();
	QString newName = item->text().trimmed();

	if (newName == oldName)
		return;

	if (!newName.isEmpty() && m_controller.sceneRenameActive(newName.toUtf8().constData()))
		return;

	if (!newName.isEmpty())
		QMessageBox::information(this, "Rename Scene", "A scene with that name already exists on this canvas.");
	refresh();
}

void SlDualScenesDock::onAdd()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, "Add Scene", "Scene name:", QLineEdit::Normal, QString(), &ok);
	name = name.trimmed();

	if (!ok || name.isEmpty())
		return;

	if (!m_controller.sceneCreate(name.toUtf8().constData()))
		QMessageBox::information(this, "Add Scene", "A scene with that name already exists on this canvas.");
}

void SlDualScenesDock::onRemove()
{
	SlDualCanvas *canvas = m_controller.canvas.get();

	if (!canvas)
		return;

	QString name = QString::fromUtf8(canvas->activeSceneName().c_str());

	if (QMessageBox::question(this, "Remove Scene", QString("Remove scene '%1' and its items?").arg(name)) != QMessageBox::Yes)
		return;

	m_controller.sceneRemoveActive();
}

void SlDualScenesDock::onMove(int direction)
{
	int row = m_list->currentRow();
	int target = row + direction;

	if (row < 0 || target < 0 || target >= m_list->count())
		return;

	m_updating = true;
	QListWidgetItem *item = m_list->takeItem(row);
	m_list->insertItem(target, item);
	m_list->setCurrentItem(item);
	m_updating = false;

	persistOrder();
}

void SlDualScenesDock::onOrderDropped()
{
	if (m_updating)
		return;

	persistOrder();
}

void SlDualScenesDock::showContextMenu(const QPoint &pos)
{
	QMenu menu(this);
	menu.addAction("Add Scene", [this]() { onAdd(); });

	// Both actions work on the current row - Rename through currentItem(), Remove through the
	// canvas's active scene - so the clicked row has to become current first, or right-clicking an
	// unselected scene acts on a different one. Selecting on right-click is what the main OBS
	// scene list does too.
	if (QListWidgetItem *clicked = m_list->itemAt(pos))
	{
		m_list->setCurrentItem(clicked);

		menu.addAction("Rename", [this]() { m_list->editItem(m_list->currentItem()); });
		menu.addAction("Remove", [this]() { onRemove(); });
	}

	menu.exec(m_list->mapToGlobal(pos));
}

std::vector<std::string> SlDualScenesDock::displayedNames() const
{
	std::vector<std::string> names;

	for (int i = 0; i < m_list->count(); i++)
		names.push_back(m_list->item(i)->text().toUtf8().constData());

	return names;
}

void SlDualScenesDock::persistOrder()
{
	m_controller.config.sceneOrder = displayedNames();
	obs_frontend_save();
}
