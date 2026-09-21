#!/bin/bash
# Search script for chadvis-projectm-qt database
# Usage: ./search_db.sh <search_string>
#
# Security: uses SQLite .param binding to prevent SQL injection.
# User input is never concatenated into SQL text.

# Resolve DB path using XDG-style location; fall back to $HOME if unset.
DB_PATH="${XDG_DATA_HOME:-$HOME/.local/share}/chadvis-projectm-qt/suno_library.db"

# Check if argument provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <search_string>"
    echo "Example: $0 'my song title'"
    exit 1
fi

SEARCH_TERM="$1"

# Escape single quotes for SQL: ' -> '' (SQL standard escaping).
# This prevents quote-breakout in the .param binding below.
ESCAPED_SEARCH_TERM="${SEARCH_TERM//\'/\'\'}"

# Check if database exists
if [ ! -f "$DB_PATH" ]; then
    echo "Error: Database not found at $DB_PATH"
    exit 1
fi

# Check if sqlite3 is available
if ! command -v sqlite3 &> /dev/null; then
    echo "Error: sqlite3 command not found"
    exit 1
fi

echo "Searching for: '$SEARCH_TERM'"
echo "================================"

# Search across multiple text fields in the clips table.
# Uses .param :search to bind the user input as a literal value —
# no string interpolation, so SQL injection is impossible.
sqlite3 "$DB_PATH" <<EOF
.headers on
.mode column
.param :search '$ESCAPED_SEARCH_TERM'
SELECT
    id,
    title,
    display_name,
    handle,
    created_at,
    CASE
        WHEN length(prompt) > 50 THEN substr(prompt, 1, 50) || '...'
        ELSE prompt
    END as prompt_preview
FROM clips
WHERE
    title LIKE '%' || :search || '%'
    OR prompt LIKE '%' || :search || '%'
    OR tags LIKE '%' || :search || '%'
    OR lyrics LIKE '%' || :search || '%'
    OR display_name LIKE '%' || :search || '%'
    OR handle LIKE '%' || :search || '%'
    OR id LIKE '%' || :search || '%'
ORDER BY created_at DESC;
EOF

echo ""
echo "================================"

# Show count — same parameterized approach.
COUNT=$(sqlite3 "$DB_PATH" <<EOF
.param :search '$ESCAPED_SEARCH_TERM'
SELECT COUNT(*) FROM clips WHERE
    title LIKE '%' || :search || '%'
    OR prompt LIKE '%' || :search || '%'
    OR tags LIKE '%' || :search || '%'
    OR lyrics LIKE '%' || :search || '%'
    OR display_name LIKE '%' || :search || '%'
    OR handle LIKE '%' || :search || '%'
    OR id LIKE '%' || :search || '%';
EOF
)
echo "Total matches: $COUNT"