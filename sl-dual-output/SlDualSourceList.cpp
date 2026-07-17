#include "SlDualSourceList.hpp"
#include "SlDualCanvas.hpp"
#include "SlDualDock.hpp"
#include "SlDualEditor.hpp"

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>

#include <utility>
#include <vector>

static const char* kSceneSignals[] = {"item_add",     "item_remove", "reorder",     "refresh",
			       "item_visible", "item_locked", "item_select", "item_deselect"};

struct RowInfo
{
	int64_t id;
	QString name;
	bool visible;
	bool selected;
};

static bool collectRowsProc(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
	// bottom to top
	auto* rows = static_cast<std::vector<RowInfo>*>(param);
	obs_source_t* source = obs_sceneitem_get_source(item);
	const char* name = source ? obs_source_get_name(source) : nullptr;

	RowInfo row;
	row.id = obs_sceneitem_get_id(item);
	row.name = QString::fromUtf8(name ? name : "(unnamed)");
	row.visible = obs_sceneitem_visible(item);
	row.selected = obs_sceneitem_selected(item);
	rows->push_back(row);
	return true;
}

SlDualSourceList::SlDualSourceList(SlDualController& controller, SlDualPreview* preview, QWidget* parent)
	: QListWidget(parent),
	  m_controller(controller),
	  m_preview(preview)
{
	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setDragDropMode(QAbstractItemView::InternalMove);
	setDefaultDropAction(Qt::MoveAction);

	QObject::connect(this, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) { onItemChanged(item); });
	QObject::connect(this, &QListWidget::itemSelectionChanged, this, [this]() { onSelectionChanged(); });
}

SlDualSourceList::~SlDualSourceList()
{
	unbindScene();
}

void SlDualSourceList::sceneSignalThunk(void* data, calldata_t*)
{
	// Scene signals can fire from the graphics thread (e.g. libobs pruning items of removed sources during a tick); hop to the UI thread.
	auto* self = static_cast<SlDualSourceList*>(data);
	QMetaObject::invokeMethod(self, [self]() { self->rebuild(); }, Qt::QueuedConnection);
}

void SlDualSourceList::unbindScene()
{
	if (!m_boundSceneSource)
		return;

	signal_handler_t* sh = obs_source_get_signal_handler(m_boundSceneSource);

	for (const char* signal : kSceneSignals)
		signal_handler_disconnect(sh, signal, sceneSignalThunk, this);

	obs_source_release(m_boundSceneSource);
	m_boundSceneSource = nullptr;
}

void SlDualSourceList::bindActiveScene()
{
	unbindScene();

	SlDualCanvas* canvas = m_controller.canvas.get();
	obs_scene_t* scene = canvas ? canvas->activeScene() : nullptr;

	if (scene)
	{
		m_boundSceneSource = obs_scene_get_source(scene);
		obs_source_get_ref(m_boundSceneSource);

		signal_handler_t* sh = obs_source_get_signal_handler(m_boundSceneSource);

		for (const char* signal : kSceneSignals)
			signal_handler_connect(sh, signal, sceneSignalThunk, this);
	}

	rebuild();
}

void SlDualSourceList::rebuild()
{
	m_updating = true;
	clear();

	SlDualCanvas* canvas = m_controller.canvas.get();
	obs_scene_t* scene = canvas ? canvas->activeScene() : nullptr;

	if (scene)
	{
		std::vector<RowInfo> rows;
		obs_scene_enum_items(scene, collectRowsProc, &rows);

		// Display topmost first, like the OBS Sources dock.
		for (auto it = rows.rbegin(); it != rows.rend(); ++it)
		{
			auto* listItem = new QListWidgetItem(it->name, this);
			listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			listItem->setCheckState(it->visible ? Qt::Checked : Qt::Unchecked);
			listItem->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(it->id));
			listItem->setSelected(it->selected);
		}
	}

	setEnabled(scene != nullptr);
	m_updating = false;
}

obs_sceneitem_t* SlDualSourceList::sceneItemForRow(int row) const
{
	SlDualCanvas* canvas = m_controller.canvas.get();
	obs_scene_t* scene = canvas ? canvas->activeScene() : nullptr;
	QListWidgetItem* listItem = item(row);

	if (!scene || !listItem)
		return nullptr;

	return obs_scene_find_sceneitem_by_id(scene, (int64_t)listItem->data(Qt::UserRole).toLongLong());
}

void SlDualSourceList::onItemChanged(QListWidgetItem* listItem)
{
	if (m_updating || !listItem)
		return;

	obs_sceneitem_t* sceneItem = sceneItemForRow(row(listItem));

	if (sceneItem && m_preview)
		m_preview->editor().setItemVisibleUndoable(sceneItem, listItem->checkState() == Qt::Checked);
}

void SlDualSourceList::onSelectionChanged()
{
	if (m_updating)
		return;

	// scene item_select signals rebuild otherwise
	m_updating = true;

	for (int i = 0; i < count(); i++)
	{
		obs_sceneitem_t* sceneItem = sceneItemForRow(i);

		if (sceneItem)
			obs_sceneitem_select(sceneItem, item(i)->isSelected());
	}

	m_updating = false;
}

void SlDualSourceList::removeSelected()
{
	if (m_preview)
		m_preview->editor().removeSelectedItemsPublic(this);
}

void SlDualSourceList::openSelectedProperties()
{
	obs_sceneitem_t* sceneItem = sceneItemForRow(currentRow());

	if (!sceneItem)
		return;

	obs_source_t* source = obs_sceneitem_get_source(sceneItem);

	if (source && obs_source_configurable(source))
		obs_frontend_open_source_properties(source);
}

void SlDualSourceList::moveSelected(int direction)
{
	int row = currentRow();
	int target = row + direction;

	if (row < 0 || target < 0 || target >= count() || !m_preview)
		return;

	// Rows are topmost-first; scene order is bottom to top.
	std::vector<int64_t> order;

	for (int i = count() - 1; i >= 0; i--)
		order.push_back((int64_t)item(i)->data(Qt::UserRole).toLongLong());

	size_t a = (size_t)(count() - 1 - row);
	size_t b = (size_t)(count() - 1 - target);
	std::swap(order[a], order[b]);
	m_preview->editor().applyOrderUndoable(order);
}

void SlDualSourceList::contextMenuEvent(QContextMenuEvent* event)
{
	QListWidgetItem* listItem = itemAt(event->pos());

	if (!listItem)
	{
		// Empty space: same menu as empty-space right-click on the preview.
		if (m_preview)
			m_preview->editor().showSceneMenu(event->globalPos(), this);

		event->accept();
		return;
	}

	obs_sceneitem_t* sceneItem = sceneItemForRow(row(listItem));

	if (!sceneItem || !m_preview)
		return;

	if (!obs_sceneitem_selected(sceneItem))
	{
		m_updating = true;
		clearSelection();
		listItem->setSelected(true);
		m_updating = false;
		onSelectionChanged();
	}

	m_preview->editor().showItemMenu(sceneItem, event->globalPos(), this);
	event->accept();
}

void SlDualSourceList::keyPressEvent(QKeyEvent* event)
{
	if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_preview)
	{
		m_preview->editor().removeSelectedItemsPublic(this);
		event->accept();
		return;
	}

	QListWidget::keyPressEvent(event);
}

void SlDualSourceList::mouseDoubleClickEvent(QMouseEvent* event)
{
	QListWidgetItem* listItem = itemAt(event->position().toPoint());

	if (listItem)
	{
		obs_sceneitem_t* sceneItem = sceneItemForRow(row(listItem));

		if (sceneItem)
		{
			obs_source_t* source = obs_sceneitem_get_source(sceneItem);

			if (source && obs_source_configurable(source))
			{
				obs_frontend_open_source_properties(source);
				event->accept();
				return;
			}
		}
	}

	QListWidget::mouseDoubleClickEvent(event);
}

void SlDualSourceList::dropEvent(QDropEvent* event)
{
	QListWidget::dropEvent(event);

	if (!m_preview)
		return;

	// List shows topmost first; scene order is bottom to top.
	std::vector<int64_t> newOrderBottomToTop;

	for (int i = count() - 1; i >= 0; i--)
		newOrderBottomToTop.push_back((int64_t)item(i)->data(Qt::UserRole).toLongLong());

	m_preview->editor().applyOrderUndoable(newOrderBottomToTop);
}
