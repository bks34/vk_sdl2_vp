//
// Created for vk_sdl2_vp
//

#include "AppConfig.h"
#include "VulkanSDL2App.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
//  helpers
// ---------------------------------------------------------------------------

static std::string trim(std::string s) {
    // left
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    // right
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

// ---------------------------------------------------------------------------
//  path helpers
// ---------------------------------------------------------------------------

std::string AppConfig::configDir() {
    // SDL_GetPrefPath returns base_dir/org/app/ — using empty org
    // gives us base_dir/vk_sdl2_vp/ with a single level.
    char* path = SDL_GetPrefPath("", "vk_sdl2_vp");
    std::string dir(path);
    SDL_free(path);
    return dir;
}

std::string AppConfig::configFilePath() {
    return configDir() + "config";
}

// ---------------------------------------------------------------------------
//  valid keys / values
// ---------------------------------------------------------------------------

std::vector<std::string> AppConfig::validKeys() {
    return {"hw", "gpu", "replay"};
}

std::vector<std::string> AppConfig::validValues(const std::string& key) {
    if (key == "hw")     return {"none", "auto", "vaapi", "cuda"};
    if (key == "gpu")    return {"integrated", "discrete"};
    if (key == "replay") return {"off", "on"};
    return {};
}

// ---------------------------------------------------------------------------
//  helpers
// ---------------------------------------------------------------------------

/// Returns the default value for a valid key (used by save to fill gaps).
std::string AppConfig::defaultValue(const std::string& key) {
    if (key == "hw")     return "none";
    if (key == "gpu")    return "integrated";
    if (key == "replay") return "off";
    return "";
}

// ---------------------------------------------------------------------------
//  load / save
// ---------------------------------------------------------------------------

void AppConfig::load() {
    entries_.clear();

    std::ifstream in(configFilePath());
    if (!in.is_open()) return;   // missing file → keep hardcoded defaults

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        std::string trimmed = trim(line);

        // skip blank lines and comments
        if (trimmed.empty() || trimmed[0] == '#') continue;

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "[config] line %d: missing '=', skipping\n", lineno);
            continue;
        }

        std::string key   = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));

        if (key.empty()) {
            std::fprintf(stderr, "[config] line %d: empty key, skipping\n", lineno);
            continue;
        }

        // validate key — warn and skip unknown keys
        auto vk = validKeys();
        if (std::find(vk.begin(), vk.end(), key) == vk.end()) {
            std::fprintf(stderr, "[config] line %d: unknown key '%s', ignored\n",
                         lineno, key.c_str());
            continue;
        }

        entries_[key] = value;
    }
}

void AppConfig::save() const {
    std::ofstream out(configFilePath());
    if (!out.is_open()) {
        std::fprintf(stderr, "[config] failed to write %s\n",
                     configFilePath().c_str());
        return;
    }

    out << "# vk_sdl2_vp configuration\n";
    for (const auto& key : validKeys()) {
        auto it = entries_.find(key);
        out << key << " = " << (it != entries_.end() ? it->second : defaultValue(key)) << "\n";
    }
}

// ---------------------------------------------------------------------------
//  get / set
// ---------------------------------------------------------------------------

std::string AppConfig::get(const std::string& key) const {
    auto it = entries_.find(key);
    return it != entries_.end() ? it->second : "";
}

bool AppConfig::set(const std::string& key, const std::string& value,
                    std::string& err) {
    // validate key
    auto vk = validKeys();
    if (std::find(vk.begin(), vk.end(), key) == vk.end()) {
        std::ostringstream oss;
        oss << "Unknown key '" << key << "'. Valid keys:";
        for (const auto& k : vk) oss << " " << k;
        err = oss.str();
        return false;
    }

    // validate value
    auto vv = validValues(key);
    if (std::find(vv.begin(), vv.end(), value) == vv.end()) {
        std::ostringstream oss;
        oss << "Invalid value '" << value << "' for key '" << key << "'. Valid values:";
        for (const auto& v : vv) oss << " " << v;
        err = oss.str();
        return false;
    }

    entries_[key] = value;
    return true;
}

// ---------------------------------------------------------------------------
//  apply to Config
// ---------------------------------------------------------------------------

void AppConfig::applyTo(Config& cfg) const {
    auto it = entries_.find("hw");
    if (it != entries_.end()) {
        cfg.hwAccel = it->second;
    }

    it = entries_.find("gpu");
    if (it != entries_.end()) {
        cfg.DiscreteGpuFirst = (it->second == "discrete");
    }

    it = entries_.find("replay");
    if (it != entries_.end()) {
        cfg.autoReplay = (it->second == "on");
    }
}
