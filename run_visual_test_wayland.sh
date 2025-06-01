#!/bin/bash
# run_visual_test_wayland.sh - Run visual tests with nested Wayland

echo "🚀 Starting visual test with nested Wayland backend..."

# Check if we're in Wayland
if [[ -z "$WAYLAND_DISPLAY" ]]; then
    echo "❌ Not in Wayland session - cannot use nested backend"
    exit 1
fi

# Set environment for nested Wayland
export WLR_BACKENDS=wayland
export WLR_WL_OUTPUTS=1

echo "📱 Backend: Wayland (nested)"
echo "📱 Parent display: $WAYLAND_DISPLAY"

# Run visual demonstration
python3 test_visual_demonstration.py
