#include "VulkanSDL2App.h"
#include <iostream>
#include <set>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file> " << " <Options>... "<< std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -d       prefer discrete GPU (default: integrated first)." << std::endl;
        std::cout << "  -r       auto replay when playback finishes." << std::endl;
        std::cout << "  -hw <backend>  hardware acceleration backend." << std::endl;
        std::cout << "           none (default) / auto / vaapi / cuda" << std::endl;
        return -1;
    }

    Config config;
    std::set<std::string> options;
    for (int i = 2; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-hw" && i + 1 < argc) {
            config.hwAccel = std::string(argv[++i]);
        } else {
            options.insert(arg);
        }
    }

    if (options.count("-d")) {
        config.DiscreteGpuFirst = true;
    }
    if (options.count("-r")) {
        config.autoReplay = true;
    }

    VulkanSDL2App app(std::string(argv[1]), 800, 600, config);

    app.run();
    return 0;
}
