// SaklıBahçe — entry point: parse the command line and run the application.
#include "app/App.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // progress lines reach logs even if the run is cut short
    app::Options opt;
    std::string error;
    bool help = false;
    if (!app::parseArgs(argc, argv, opt, error, help)) {
        std::fprintf(stderr, "%s\n\n", error.c_str());
        app::printUsage(argv[0]);
        return 2;
    }
    if (help) {
        app::printUsage(argv[0]);
        return 0;
    }
    return app::run(opt);
}
