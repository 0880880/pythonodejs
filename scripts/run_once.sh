#!/bin/bash
set -e

MARKER_FILE="/tmp/build_once_done"

if [ ! -f "$MARKER_FILE" ]; then
    echo "Running pre-build commands..."
    
    "$@"
    
    touch "$MARKER_FILE"
else
    echo "Pre-build commands already ran, skipping."
fi
