#include "SlDualScenesDock.hpp"
#include "SlDualCanvas.hpp"

#include <QAbstractItemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

SlDualScenesDock::SlDualScenesDock(SlDualController& controller) : QWidget(nullptr), m_controller(controller)
{
	m_list = new QListWidget(this);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setDragDropMode(QAbstractItemView::InternalMove);
	m_list->setDefaultDropAction(Qt::MoveAction);
	m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_list->setContextMenuPolicy(Qt::CustomContextMenu);

	m_addButton = new QToolButton(this);
	m_addButton->setText("+");
	m_addButton->setToolTip("Add scene");

	m_removeButton = new QToolButton(this);
	m_removeButton->setText("-");
	m_removeButton->setToolTip("Remove scene");

	m_upButton = new QToolButton(this);
	m_upButton->setText(QString(QChar(0x25B2)));
	m_upButton->setToolTip("Move scene up");

	m_downButton = new QToolButton(this);
	m_downButton->setText(QString(QChar(0x25BC)));
	m_downButton->setToolTip("Move scene down");

	auto* toolbar = new QHBoxLayout();
	toolbar->setContentsMargins(0, 0, 0, 0);
	toolbar->addWidget(m_addButton);
	toolbar->addWidget(m_removeButton);
	toolbar->addStretch(1);
	toolbar->addWidget(m_upButton);
	toolbar->addWidget(m_downButton);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(m_list, 1);
	layout->addLayout(toolbar);

	QObject::connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { onSelectionChanged(); });
	QObject::connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) { onItemEdited(item); });
	QObject::connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showContextMenu(pos); });
	QObject::connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]() { onOrderDropped(); });
	QObject::connect(m_addButton, &QToolButton::clicked, this, [this]() { onAdd(); });
	QObject::connect(m_removeButton, &QToolButton::clicked, this, [this]() { onRemove(); });
	QObject::connect(m_upButton, &QToolButton::clicked, this, [this]() { onMove(-1); });
	QObject::connect(m_downButton, &QToolButton::clicked, this, [this]() { onMove(1); });

	refresh();
}

void SlDualScenesDock::refresh()
{
	m_updating = true;
	m_list->clear();

	SlDualCanvas* canvas = m_controller.canvas.get();
	bool haveCanvas = canvas && canvas->valid();

	if (haveCanvas)
	{
		// config.sceneOrder decides display order; unknown scenes append, stale names drop.
		std::vector<std::string> names = canvas->sceneNames();
		std::vector<std::string> ordered;

		for (const std::string& name : m_controller.config.sceneOrder)
		{
			bool known = std::find(names.begin(), names.end(), name) != names.end();
			bool seen = std::find(ordered.begin(), ordered.end(), name) != ordered.end();

			if (known && !seen)
				ordered.push_back(name);
		}

		for (const std::string& name : names)
		{
			if (std::find(ordered.begin(), ordered.end(), name) == ordered.end())
				ordered.push_back(name);
		}

		m_controller.config.sceneOrder = ordered;
		std::string active = canvas->activeSceneName();

		for (const std::string& name : ordered)
		{
			auto* item = new QListWidgetItem(QString::fromUtf8(name.c_str()), m_list);
			item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

			// original name, for rename validation
			item->setData(Qt::UserRole, QString::fromUtf8(name.c_str()));

			if (name == active)
				m_list->setCurrentItem(item);
		}
	}

	m_list->setEnabled(haveCanvas);
	m_addButton->setEnabled(haveCanvas);
	m_removeButton->setEnabled(m_list->count() > 1);
	m_upButton->setEnabled(m_list->count() > 1);
	m_downButton->setEnabled(m_list->count() > 1);
	m_updating = false;
}

void SlDualScenesDock::onSelectionChanged()
{
	if (m_updating)
		return;

	QListWidgetItem* item = m_list->currentItem();

	if (!item)
		return;

	std::string name = item->text().toUtf8().constData();
	SlDualCanvas* canvas = m_controller.canvas.get();

	if (canvas && name != canvas->activeSceneName())
		m_controller.sceneSetActive(name);
}

void SlDualScenesDock::onItemEdited(QListWidgetItem* item)
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
	SlDualCanvas* canvas = m_controller.canvas.get();

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
	QListWidgetItem* item = m_list->takeItem(row);
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

void SlDualScenesDock::showContextMenu(const QPoint& pos)
{
	QMenu menu(this);
	menu.addAction("Add Scene", [this]() { onAdd(); });

	if (m_list->itemAt(pos))
	{
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
