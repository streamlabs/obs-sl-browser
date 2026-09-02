#pragma once

// Module-internal.
// Transition instances for the dual canvas, mirroring the main Scene Transitions dock's model:
// one private instance per non-configurable type (Cut, Fade), plus user-added configurable instances persisted with the collection.

#include "SlDualConfig.hpp"

#include <obs.h>

#include <string>
#include <vector>

class SlDualTransitions
{
public:
	SlDualTransitions() = default;
	~SlDualTransitions();

	SlDualTransitions(const SlDualTransitions&) = delete;
	SlDualTransitions& operator=(const SlDualTransitions&) = delete;

	// Builds instances from the installed types plus config.customTransitions; drops any previous set.
	void rebuild(const SlDualConfig& config);
	void clear();

	// borrowed
	obs_source_t* find(const std::string& name) const;

	// config.transitionName resolved with fallbacks (Fade, then the first instance).
	obs_source_t* selected(const SlDualConfig& config) const;
	std::string selectedName(const SlDualConfig& config) const;

	std::vector<std::string> names() const;
	bool configurable(const std::string& name) const;

	// Creates a private instance of a configurable type; fails on duplicate name.
	bool add(const std::string& typeId, const std::string& name);

	// Configurable instances only.
	bool remove(const std::string& name);

	// Current settings of the configurable instances, for persistence.
	std::vector<SlDualTransitionInfo> customInfos() const;

private:
	struct Entry
	{
		// owned reference
		obs_source_t* source = nullptr;
		std::string id;
		std::string name;
		bool configurable = false;
	};

	const Entry* entryByName(const std::string& name) const;

	std::vector<Entry> m_entries;
};
