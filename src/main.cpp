#include "VulkanSDL2App.h"
#include <iostream>


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file>" << std::endl;
        return -1;
    }
    VulkanSDL2App app(std::string(argv[1]), 1920, 1080);
    app.run();
    return 0;
}
