#include "viewer/app/Application.hpp"

int main(int argc, char** argv) {
    const char* config_path = "configs/default.json";
    if (argc > 1) config_path = argv[1];
    viewer::Application app;
    if (!app.initialize(config_path)) return -1;
    app.run();
    app.shutdown();
    return 0;
}
