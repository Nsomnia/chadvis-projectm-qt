#!/bin/bash
# Search script for chadvis-projectm-qt database
# Usage: ./search_db.sh <search_string>

DB_PATH="/home/nsomnia/.local/share/chadvis-projectm-qt/suno_library.db"

# Check if argument provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <search_string>"
    echo "Example: $0 'my song title'"
    exit 1
fi

SEARCH_TERM="$1"

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

# Search across multiple text fields in the clips table
sqlite3 "$DB_PATH" <<EOF
.headers on
.mode column
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
    title LIKE '%${SEARCH_TERM}%'
    OR prompt LIKE '%${SEARCH_TERM}%'
    OR tags LIKE '%${SEARCH_TERM}%'
    OR lyrics LIKE '%${SEARCH_TERM}%'
    OR display_name LIKE '%${SEARCH_TERM}%'
    OR handle LIKE '%${SEARCH_TERM}%'
    OR id LIKE '%${SEARCH_TERM}%'
ORDER BY created_at DESC;
EOF

echo ""
echo "================================"

# Show count
COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM clips WHERE title LIKE '%${SEARCH_TERM}%' OR prompt LIKE '%${SEARCH_TERM}%' OR tags LIKE '%${SEARCH_TERM}%' OR lyrics LIKE '%${SEARCH_TERM}%' OR display_name LIKE '%${SEARCH_TERM}%' OR handle LIKE '%${SEARCH_TERM}%' OR id LIKE '%${SEARCH_TERM}%';")
echo "Total matches: $COUNT"
