#include "SlDualUndo.hpp"

void SlDualUndo::add(std::string name, Action undo, Action redo, std::string undoData, std::string redoData)
{
	m_stack.resize(m_pos); // drop any redoable tail

	Entry entry;
	entry.name = std::move(name);
	entry.undo = std::move(undo);
	entry.redo = std::move(redo);
	entry.undoData = std::move(undoData);
	entry.redoData = std::move(redoData);
	m_stack.push_back(std::move(entry));

	if (m_stack.size() > kMaxEntries)
		m_stack.erase(m_stack.begin());

	m_pos = m_stack.size();
}

bool SlDualUndo::undo()
{
	if (m_pos == 0)
		return false;

	Entry &entry = m_stack[--m_pos];
	if (entry.undo)
		entry.undo(entry.undoData);
	return true;
}

bool SlDualUndo::redo()
{
	if (m_pos >= m_stack.size())
		return false;

	Entry &entry = m_stack[m_pos++];
	if (entry.redo)
		entry.redo(entry.redoData);
	return true;
}

void SlDualUndo::clear()
{
	m_stack.clear();
	m_pos = 0;
}
