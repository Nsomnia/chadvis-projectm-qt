# Task
Investigate the codebase within the following files and directories recursively to find means of improving the project in aboslutely any aspect. You may orchestrate any number of sub-agents if deemed appropriate for a task. You have no token usage limits of either input, output, nor thinking. These documents are almost purely for LLM AI model h input handling agentically and thus human readability is of lesser concern. You may optionally rank findinds or other organization means where appropriate. Think in various forms, styles, and roles, that a development team may work in, such as senior developer, quality contrl, bug bounty finder, refactor analyzer, codebase size checker, and other such roles.

# Project File List
```
cmake/
CMakeLists.txt
.prettierignore
config/
README.md
AGENTS.md
assets/
resources/
skills/
src/
tests/
.clangd
.clang-tidy
```

# Output
Write your recomendations into one or more files of appropriate filetype and format, that also must contain your model name in the filename to differentiate it from others, in the projects root directory with your findings for project improvments given being past 20k LOC. Notably all QT web browser based logic can be removed as it is not viable for authentication with remote servers such as google oauth login and means of authenticating via the user manually finding strings in their browser dev tools or via a localhost callback url have been found.

You are free to use sub-agents to maintain context limit windows which they will report back to you.
