#include "VulkanSDL2App.h"
#include <iostream>
#include <set>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file> " << " <Options>... "<< std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "-d for discrete gpu first(default integrated first)." << std::endl;
        std::cout << "-r for replay(default not)." << std::endl;
        return -1;
    }

    std::set<std::string> options;
    for (int i = 2; i < argc; ++i) {
        options.insert(std::string(argv[i]));
    }

    Config config;
    if (options.count("-d")) {
        config.DiscreteGpuFirst = true;
    }
    if (options.count("-r")) {
        config.autoReplay = true;
    }

    VulkanSDL2App app(std::string(argv[1]), 1920, 1080, config);
    app.run();
    return 0;
}
