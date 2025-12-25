#!/usr/bin/env bash
# Dump a webpage for parsing
if [ -z "$1" ]; then
echo "Usage: $0 <url>"
exit 1
fi
URL="$1"
if command -v lynx &>/dev/null; then
lynx -dump "$URL"
elif command -v links &>/dev/null; then
links -dump "$URL"
else
echo "No terminal browser found. Install lynx or links."
exit 1
fi
