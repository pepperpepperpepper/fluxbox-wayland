#!/bin/bash
# Fluxbox Wayland with XDG Portal Screenshot Support

echo "🌟 Starting Fluxbox Wayland with Portal Backend"
echo "=============================================="

# Set environment
export DISPLAY=:1
export WLR_BACKENDS=x11

# Kill any existing instances
echo "🧹 Cleaning up existing processes..."
killall fluxbox-wayland 2>/dev/null
killall fluxbox-portal-backend 2>/dev/null
sleep 2

# Start compositor in background
echo "🚀 Starting Fluxbox Wayland compositor..."
./build/fluxbox-wayland &
COMPOSITOR_PID=$!

# Wait for compositor to start
sleep 3

# Check if compositor is running
if ! ps -p $COMPOSITOR_PID > /dev/null; then
    echo "❌ Compositor failed to start"
    exit 1
fi

# Start portal backend
echo "📡 Starting XDG Desktop Portal backend..."
./build/fluxbox-portal-backend &
PORTAL_PID=$!

# Wait for portal to start
sleep 2

# Check if portal is running
if ! ps -p $PORTAL_PID > /dev/null; then
    echo "❌ Portal backend failed to start"
    kill $COMPOSITOR_PID 2>/dev/null
    exit 1
fi

echo ""
echo "✅ Fluxbox Wayland with Portal Backend is running!"
echo ""
echo "🎯 Ready for screenshots:"
echo "   • Use any portal-aware screenshot tool"
echo "   • Try: gnome-screenshot, spectacle, or portal test"
echo ""
echo "📊 Running processes:"
echo "   • Compositor PID: $COMPOSITOR_PID"
echo "   • Portal PID: $PORTAL_PID"
echo ""
echo "Press Ctrl+C to stop both services"

# Set up signal handler to clean up both processes
cleanup() {
    echo ""
    echo "🛑 Shutting down..."
    kill $PORTAL_PID 2>/dev/null
    kill $COMPOSITOR_PID 2>/dev/null
    exit 0
}

trap cleanup SIGINT SIGTERM

# Keep script running
wait