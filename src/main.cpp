// main.cpp - ChadVis Entry Point
// "Hello, World!" but with more bass drops
//
// ██╗   ██╗██╗██████╗ ███████╗ ██████╗██╗  ██╗ █████╗ ██████╗ 
// ██║   ██║██║██╔══██╗██╔════╝██╔════╝██║  ██║██╔══██╗██╔══██╗
// ██║   ██║██║██████╔╝█████╗  ██║     ███████║███████║██║  ██║
// ╚██╗ ██╔╝██║██╔══██╗██╔══╝  ██║     ██╔══██║██╔══██║██║  ██║
//  ╚████╔╝ ██║██████╔╝███████╗╚██████╗██║  ██║██║  ██║██████╔╝
//   ╚═══╝  ╚═╝╚═════╝ ╚══════╝ ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝ 
//
// I use Arch btw.

#include "core/Application.hpp"
#include "core/Logger.hpp"

#include <iostream>
#include <csignal>

int main(int argc, char* argv[]) {
    try {
        // Create application
        vc::Application app(argc, argv);

        // Parse command line arguments
        auto optsResult = app.parseArgs();
        if (!optsResult) {
            std::cerr << "Error: " << optsResult.error().message << "\n";
            std::cerr << "Try --help for usage information.\n";
            return 1;
        }
        
        auto opts = std::move(optsResult).value();
        
        // Initialize application
        auto initResult = app.init(opts);
        if (!initResult) {
            std::cerr << "Initialization failed: " << initResult.error().message << "\n";
            return 1;
        }
        
        // Run the event loop
        int result = app.exec();
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred.\n";
        return 1;
    }
}