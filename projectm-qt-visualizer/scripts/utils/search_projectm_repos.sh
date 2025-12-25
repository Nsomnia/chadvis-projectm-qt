#!/usr/bin/env bash
# Search GitHub for projectM related repositories
echo "=== Searching projectM Repositories ==="
echo ""
echo "--- Official projectM repos ---"
gh search repos --include-forks true --sort updated --user projectm-visualizer --limit 10
echo ""
echo "--- Projects using projectM v4 ---"
gh search repos "libprojectm" --language=cpp --sort=stars --limit 10
echo ""
echo "--- Qt + projectM projects ---"
gh search repos "projectm qt" --sort=updated --limit 10
