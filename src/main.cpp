#include "VulkanSDL2App.h"
#include "AppConfig.h"
#include <iostream>

static void printUsage(const char* argv0) {
    std::cout << "Usage:\n"
              << "  " << argv0 << " <file> [Options...]\n"
              << "  " << argv0 << " set <key> <value>\n"
              << "  " << argv0 << " get [key]\n"
              << "\nPlayback Options:\n"
              << "  -gpu i/d         GPU preference (i=integrated, d=discrete).\n"
              << "  -r on/off        auto replay when playback finishes.\n"
              << "  -hw <backend>    hardware acceleration backend (none / auto / vaapi / cuda).\n"
              << "\nConfig keys (for set/get):\n"
              << "  hw               none / auto / vaapi / cuda\n"
              << "  gpu              integrated / discrete\n"
              << "  replay           off / on\n";
}

int main(int argc, char* argv[]) {
    // ------------------------------------------------------------------
    //  subcommand: set <key> <value>
    // ------------------------------------------------------------------
    if (argc >= 2 && std::string(argv[1]) == "set") {
        if (argc != 4) {
            std::cerr << "Usage: " << argv[0] << " set <key> <value>\n";
            return -1;
        }

        AppConfig cfg;
        cfg.load();

        std::string err;
        if (!cfg.set(argv[2], argv[3], err)) {
            std::cerr << "Error: " << err << "\n";
            return -1;
        }

        cfg.save();
        std::cout << argv[2] << " = " << argv[3] << "\n";
        return 0;
    }

    // ------------------------------------------------------------------
    //  subcommand: get [key]
    // ------------------------------------------------------------------
    if (argc >= 2 && std::string(argv[1]) == "get") {
        AppConfig cfg;
        cfg.load();

        if (argc >= 3) {
            // get single key
            std::string val = cfg.get(argv[2]);
            if (val.empty()) {
                val = AppConfig::defaultValue(argv[2]);
                if (val[0] == '\0') {
                    std::cerr << "Unknown key: " << argv[2] << "\n";
                    return -1;
                }
                std::cout << argv[2] << " = " << val << "  (default)\n";
            } else {
                std::cout << argv[2] << " = " << val << "\n";
            }
        } else {
            // get all keys
            for (const auto& key : AppConfig::validKeys()) {
                std::string val = cfg.get(key);
                bool isDefault = val.empty();
                if (isDefault) val = AppConfig::defaultValue(key);
                std::cout << key << " = " << val
                          << (isDefault ? "  (default)" : "") << "\n";
            }
        }
        return 0;
    }

    // ------------------------------------------------------------------
    //  playback path
    // ------------------------------------------------------------------
    if (argc < 2) {
        printUsage(argv[0]);
        return -1;
    }

    // 1. Load config defaults
    Config config;
    AppConfig appCfg;
    appCfg.load();
    appCfg.applyTo(config);

    // 2. Parse CLI args (override config defaults)
    for (int i = 2; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-hw" && i + 1 < argc) {
            config.hwAccel = std::string(argv[++i]);
        } else if (arg == "-gpu" && i + 1 < argc) {
            std::string val(argv[++i]);
            if (val == "d")      config.DiscreteGpuFirst = true;
            else if (val == "i") config.DiscreteGpuFirst = false;
            else std::cerr << "Warning: unknown -gpu value '" << val
                           << "', expected i or d\n";
        } else if (arg == "-r" && i + 1 < argc) {
            std::string val(argv[++i]);
            if (val == "on")     config.autoReplay = true;
            else if (val == "off") config.autoReplay = false;
            else std::cerr << "Warning: unknown -r value '" << val
                           << "', expected on or off\n";
        }
    }

    VulkanSDL2App app(std::string(argv[1]), 800, 600, config);

    app.run();
    return 0;
}
