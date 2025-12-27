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

namespace {

vc::Application* g_app = nullptr;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived signal " << signal << ", shutting down gracefully...\n";
        if (g_app) {
            g_app->quit();
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    // Setup signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        // Create application
        vc::Application app(argc, argv);
        g_app = &app;
        
        // Parse command line arguments
        auto optsResult = app.parseArgs();
        if (!optsResult) {
            std::cerr << "Error: " << optsResult.error().message << "\n";
            std::cerr << "Try --help for usage information.\n";
            return 1;
        }
        
        auto opts = std::move(*optsResult);
        
        // Initialize application
        auto initResult = app.init(opts);
        if (!initResult) {
            std::cerr << "Initialization failed: " << initResult.error().message << "\n";
            return 1;
        }
        
        // Run the event loop
        int result = app.exec();
        
        g_app = nullptr;
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred.\n";
        return 1;
    }
}