#include "VulkanSDL2App.h"
#include <iostream>


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file> " << " -d/-i(Optional) "<< std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "-d/-i for discrete/integrated gpu first." << std::endl;
        return -1;
    }
    bool DiscreteGpuFirst = false;
    if (argv[2]) {
        if (argv[2][1] == 'd') {
            DiscreteGpuFirst = true;
        }
    }
    VulkanSDL2App app(std::string(argv[1]), 1920, 1080, DiscreteGpuFirst);
    app.run();
    return 0;
}
