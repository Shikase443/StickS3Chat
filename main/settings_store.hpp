#pragma once
#include "app_types.hpp"
class SettingsStore {
public:
    bool begin();
    Settings load() const;
    bool save(const Settings&) const;
};
