#pragma once

// Module-internal.
// Undo/redo stack for dual-canvas editing, mirroring the snapshot pattern OBS's preview uses (scene transform-state save/load).

#include <functional>
#include <string>
#include <vector>

class SlDualUndo
{
public:
	using Action = std::function<void(const std::string& data)>;

	void add(std::string name, Action undo, Action redo, std::string undoData, std::string redoData);
	bool undo();
	bool redo();
	void clear();

private:
	struct Entry
	{
		std::string name;
		Action undo;
		Action redo;
		std::string undoData;
		std::string redoData;
	};

	static const size_t kMaxEntries = 64;

	std::vector<Entry> m_stack;

	// entries [0, m_pos) have been applied
	size_t m_pos = 0;
};
