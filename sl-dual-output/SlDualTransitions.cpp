#include "SlDualTransitions.hpp"

SlDualTransitions::~SlDualTransitions()
{
	clear();
}

void SlDualTransitions::clear()
{
	for (Entry& entry : m_entries)
		obs_source_release(entry.source);

	m_entries.clear();
}

void SlDualTransitions::rebuild(const SlDualConfig& config)
{
	clear();

	// Non-configurable types get one fixed instance each, like the main dock's defaults (Cut, Fade).
	size_t idx = 0;
	const char* id = nullptr;

	while (obs_enum_transition_types(idx++, &id))
	{
		if (obs_is_source_configurable(id))
			continue;

		const char* display = obs_source_get_display_name(id);
		const char* name = display ? display : id;
		obs_source_t* source = obs_source_create_private(id, name, nullptr);

		if (!source)
			continue;

		Entry entry;
		entry.source = source;
		entry.id = id;
		entry.name = name;
		m_entries.push_back(entry);
	}

	for (const SlDualTransitionInfo& info : config.customTransitions)
	{
		if (find(info.name))
			continue;

		obs_data_t* settings = info.settingsJson.empty() ? nullptr : obs_data_create_from_json(info.settingsJson.c_str());
		obs_source_t* source = obs_source_create_private(info.id.c_str(), info.name.c_str(), settings);

		if (settings)
			obs_data_release(settings);

		if (!source)
		{
			blog(LOG_WARNING, SL_DUAL_LOG_PREFIX "transition type '%s' unavailable, dropping '%s'", info.id.c_str(), info.name.c_str());
			continue;
		}

		Entry entry;
		entry.source = source;
		entry.id = info.id;
		entry.name = info.name;
		entry.configurable = true;
		m_entries.push_back(entry);
	}
}

const SlDualTransitions::Entry* SlDualTransitions::entryByName(const std::string& name) const
{
	for (const Entry& entry : m_entries)
	{
		if (entry.name == name)
			return &entry;
	}

	return nullptr;
}

obs_source_t* SlDualTransitions::find(const std::string& name) const
{
	const Entry* entry = entryByName(name);
	return entry ? entry->source : nullptr;
}

obs_source_t* SlDualTransitions::selected(const SlDualConfig& config) const
{
	if (const Entry* entry = entryByName(config.transitionName))
		return entry->source;

	for (const Entry& entry : m_entries)
	{
		if (entry.id == "fade_transition")
			return entry.source;
	}

	return m_entries.empty() ? nullptr : m_entries.front().source;
}

std::string SlDualTransitions::selectedName(const SlDualConfig& config) const
{
	obs_source_t* source = selected(config);

	for (const Entry& entry : m_entries)
	{
		if (entry.source == source)
			return entry.name;
	}

	return std::string();
}

std::vector<std::string> SlDualTransitions::names() const
{
	std::vector<std::string> result;

	for (const Entry& entry : m_entries)
		result.push_back(entry.name);

	return result;
}

bool SlDualTransitions::configurable(const std::string& name) const
{
	const Entry* entry = entryByName(name);
	return entry && entry->configurable;
}

bool SlDualTransitions::add(const std::string& typeId, const std::string& name)
{
	if (name.empty() || find(name))
		return false;

	obs_source_t* source = obs_source_create_private(typeId.c_str(), name.c_str(), nullptr);

	if (!source)
		return false;

	Entry entry;
	entry.source = source;
	entry.id = typeId;
	entry.name = name;
	entry.configurable = true;
	m_entries.push_back(entry);
	return true;
}

bool SlDualTransitions::remove(const std::string& name)
{
	for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
	{
		if (it->name != name)
			continue;

		if (!it->configurable)
			return false;

		obs_source_release(it->source);
		m_entries.erase(it);
		return true;
	}

	return false;
}

std::vector<SlDualTransitionInfo> SlDualTransitions::customInfos() const
{
	std::vector<SlDualTransitionInfo> result;

	for (const Entry& entry : m_entries)
	{
		if (!entry.configurable)
			continue;

		SlDualTransitionInfo info;
		info.id = entry.id;
		info.name = entry.name;

		obs_data_t* settings = obs_source_get_settings(entry.source);

		if (settings)
		{
			const char* json = obs_data_get_json(settings);
			info.settingsJson = json ? json : "";
			obs_data_release(settings);
		}

		result.push_back(info);
	}

	return result;
}
