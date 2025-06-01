#!/bin/bash
# run_visual_test_x11.sh - Run visual tests with X11 backend

echo "🚀 Starting visual test with X11 backend..."

# Check if we have X11
if [[ -z "$DISPLAY" ]]; then
    echo "❌ No X11 display available"
    exit 1
fi

# Set environment for X11 backend
export WLR_BACKENDS=x11
export WLR_X11_OUTPUTS=1

echo "📱 Backend: X11"
echo "📱 Display: $DISPLAY"

# Run visual demonstration
python3 test_visual_demonstration.py
