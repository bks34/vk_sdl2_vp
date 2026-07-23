//
// Created for vk_sdl2_vp
//

#ifndef VK_SDL2_VP_APPCONFIG_H
#define VK_SDL2_VP_APPCONFIG_H

#include <map>
#include <string>
#include <vector>

struct Config;

class AppConfig {
public:
    /// Directory where the config file lives (auto-created by SDL_GetPrefPath).
    static std::string configDir();

    /// Full path to the config file.
    static std::string configFilePath();

    /// Parse the config file from disk.  Missing file is not an error —
    /// entries remain empty (hardcoded defaults apply).
    void load();

    /// Write current entries to the config file.
    void save() const;

    /// Get a single key's value.  Returns empty string if key not present.
    std::string get(const std::string& key) const;

    /// Set a key.  Returns true on success; false if key or value is invalid,
    /// and writes the error message to `err`.
    bool set(const std::string& key, const std::string& value,
             std::string& err);

    /// Populate a Config struct with values from this config;
    /// missing keys keep their hardcoded defaults.
    void applyTo(Config& cfg) const;

    /// Return all valid key names.
    static std::vector<std::string> validKeys();

    /// Return valid values for a given key (empty = not a valid key).
    static std::vector<std::string> validValues(const std::string& key);

    /// Return the hardcoded default value for a key; empty if unknown.
    static std::string defaultValue(const std::string& key);

private:
    std::map<std::string, std::string> entries_;
};

#endif // VK_SDL2_VP_APPCONFIG_H
