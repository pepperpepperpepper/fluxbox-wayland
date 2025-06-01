#!/bin/bash
# run_visual_test_xvfb.sh - Run visual tests with Xvfb

echo "🚀 Starting visual test with Xvfb virtual display..."

# Check for Xvfb
if ! command -v Xvfb >/dev/null 2>&1; then
    echo "❌ Xvfb not available"
    exit 1
fi

# Start Xvfb on display :99
echo "📺 Starting Xvfb on :99..."
Xvfb :99 -screen 0 1024x768x24 &
XVFB_PID=$!

# Wait for Xvfb to start
sleep 2

# Set environment for X11 backend with Xvfb
export DISPLAY=:99
export WLR_BACKENDS=x11
export WLR_X11_OUTPUTS=1

echo "📱 Backend: X11 (Xvfb)"
echo "📱 Virtual display: :99"

# Run visual demonstration
python3 test_visual_demonstration.py

# Cleanup Xvfb
kill $XVFB_PID 2>/dev/null
