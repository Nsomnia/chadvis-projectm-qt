# Main TODO.md (User and model controlled. Note: Only the user can remove items when verified as finished  to an appropriate level. 
- [ ] QT Refactor
The qt implantation is either poorly designed, and thus not sleek, modern, not stunning, or is complex while not having much "vibe", and even non-functional such as in the case of the themes (gruvbox, dark, Tokyo Midnight, catppachino, etc) implementation.


Refactoring a complex C++ codebase (10,000+ lines of code) requires an approach that deeply decouples your business logic from your UI logic. If your current GUI implementation is heavily coupled, moving to a modern framework will allow you to drastically reduce C++ boilerplate by abstracting the UI into declarative files or clean, lightweight classes. 

Here is a curated list of the best architectural approaches and libraries for modernizing a Qt C++ application, covering your specific needs for sliders, checkboxes, panels, canvases, menu bars, windows, and dials.

### 1. Qt Quick / QML (The Modern Industry Standard)
If you want the most stunning, modern UI possible within the Qt ecosystem, **Qt Quick (QML)** is the official path forward. It acts as a complete paradigm shift from traditional `QtWidgets`.
*   **How it reduces complexity:** You move 100% of your UI layout, styling, and animations out of C++ and into declarative QML files (which look similar to CSS/JSON). Your C++ codebase becomes strictly backend logic. You expose C++ to the UI by inheriting from `QObject`, using `Q_PROPERTY`, and importing those headers into your QML engine.
*   **Handling your requirements:**
    *   *Widgets, Sliders, Checkboxes, Menu bars:* Fully supported via `QtQuick.Controls`. They are natively hardware-accelerated and support touch, fluid animations, and custom styling out of the box. 
    *   *Dials & Canvases:* QML provides native `Dial` controls and a HTML5-like `Canvas` type for 2D drawing. For complex, high-performance C++ canvases, you can create a `QQuickPaintedItem` in C++, expose the header to QML, and render it inside the modern UI.
*   **Best for:** Projects where you want a complete architectural separation between UI and backend logic, resulting in a UI that rivals modern web or React Native apps in fluidity.

### 2. QSkinny (Lightweight Pure C++ UI over Scene Graph)
If your team wants the hardware-accelerated rendering and modern feel of Qt Quick, but **strictly wants to write the UI in C++** (avoiding QML/JavaScript), **QSkinny** is the premier choice. 
*   **How it reduces complexity:** It is built directly on top of the Qt Scene Graph. Instead of heavyweight `QWidget` inheritance, QSkinny uses extremely lightweight C++ nodes. It separates the API of the controls from the styling/rendering, meaning your UI code remains incredibly clean and header-focused.
*   **Handling your requirements:** It provides a C++ API very similar to standard QtWidgets. You instantiate buttons, layouts, and sliders in pure C++, but they are rendered by the modern GPU backend.
*   **Best for:** Teams who despise context-switching between C++ and QML but want an automotive-grade, highly performant, and deeply modular modern C++ UI.

### 3. QFluentWidgets (Modernizing Traditional Qt Widgets)
If rewriting your 10,000+ LOC architecture to separate frontend from backend is too risky, you can keep your existing `QWidget` architecture but completely overhaul the aesthetics using third-party C++ styling libraries like **QFluentWidgets**.
*   **How it reduces complexity:** It acts as a drop-in replacement. Instead of writing custom QSS (Qt Style Sheets) and overriding `paintEvent` functions to make legacy widgets look modern, you just `#include` the Fluent UI widget headers. 
*   **Handling your requirements:** It provides a stunning, modern Material/Windows 11 "Fluent" design out of the box for all standard elements: Windows, panels, sliders, checkboxes, and buttons.
*   **Best for:** Legacy codebases where the C++ logic is deeply tangled with `QMainWindow` and `QWidget` pointers, but the product desperately needs a modern facelift. (Alternative: **qtmodern** for a sleek dark theme drop-in).

### 4. KDDockWidgets by KDAB (For Advanced Panels and Windows)
If your application is highly technical (like an IDE, CAD software, or video editor) and relies heavily on complex **panels, dockable windows, and menu bars**, the native `QDockWidget` is notoriously buggy and visually dated.
*   **How it reduces complexity:** KDAB (one of the largest contributors to the Qt project) created **KDDockWidgets**. It is a massive framework improvement that provides modern tear-off panels, advanced nesting, and floating windows.
*   **Best for:** Modularizing a complex, multi-monitor desktop application. You can easily import their clean headers and instantly gain enterprise-grade window management that looks and behaves like modern Visual Studio or Adobe software. 

### 5. QCustomPlot / Qwt (For High-Performance Canvases and Dials)
If your legacy "dials and canvases" require high-performance technical plotting (e.g., oscilloscopes, medical data, real-time metrics), standard Qt widgets will struggle.
*   **QCustomPlot:** Offers stunning, modern 2D plotting and canvas handling. It is incredibly easy to import—it consists of literally just two files (`qcustomplot.h` and `qcustomplot.cpp`) that you drop into your codebase. It reduces thousands of lines of custom OpenGL/QPainter canvas code into a few API calls.
*   **Qwt (Qt Widgets for Technical Applications):** The industry standard for complex technical controls. While slightly less "modern" looking out of the box than QCustomPlot, it provides incredibly robust **Dials, Knobs, Compasses, and Thermometers** native to C++.

### Summary Recommendation for Your Refactor:

1.  **The "Clean Break" Approach:** Migrate the UI entirely to **QML / Qt Quick**. Expose your massive C++ codebase as backend services via headers and `Q_INVOKABLE` macros. This is the cleanest, most modern approach for standard UI elements (sliders, checkboxes, dials).
2.  **The "Pure C++" Modern Approach:** Use **QSkinny** to get modern, hardware-accelerated graphics without ever leaving C++.
3.  **The "Ship it Fast" Approach:** Keep your current `QWidget` architecture. Swap your standard widgets for **QFluentWidgets** to make it stunning. Swap your messy custom panels for **KDDockWidgets**. Swap your custom canvas/drawing code for **QCustomPlot**.

- [ ] Suno with notes
Something regarding the big gh projects particularly that sickkkkk browser extensions that does everything within js had with auth handling done perfectly. 
- [ ] Icons and UI elements in general:
Use Gemini 3.1 pro and others to develop large, sleek, theme appropriate, icons and other theme elements. 