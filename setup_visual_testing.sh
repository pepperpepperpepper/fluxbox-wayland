#!/bin/bash
# setup_visual_testing.sh - Setup environment for visual testing and screenshots

set -e

echo "============================================================"
echo "    FLUXBOX WAYLAND COMPOSITOR - VISUAL TESTING SETUP"
echo "============================================================"

# Check for required tools
check_tool() {
    local tool="$1"
    local package="$2"
    if command -v "$tool" >/dev/null 2>&1; then
        echo "✅ $tool: Available"
        return 0
    else
        echo "❌ $tool: Not found (install with: $package)"
        return 1
    fi
}

echo "🔍 Checking for screenshot and display tools..."

# Screenshot tools
SCREENSHOT_TOOLS=0
check_tool "grim" "pacman -S grim" && SCREENSHOT_TOOLS=$((SCREENSHOT_TOOLS + 1))
check_tool "scrot" "pacman -S scrot" && SCREENSHOT_TOOLS=$((SCREENSHOT_TOOLS + 1))
check_tool "convert" "pacman -S imagemagick" && SCREENSHOT_TOOLS=$((SCREENSHOT_TOOLS + 1))

# Display tools
DISPLAY_TOOLS=0
check_tool "Xvfb" "pacman -S xorg-server-xvfb" && DISPLAY_TOOLS=$((DISPLAY_TOOLS + 1))
check_tool "weston" "pacman -S weston" && DISPLAY_TOOLS=$((DISPLAY_TOOLS + 1))

# Input simulation tools
INPUT_TOOLS=0
check_tool "wtype" "pacman -S wtype" && INPUT_TOOLS=$((INPUT_TOOLS + 1))
check_tool "ydotool" "pacman -S ydotool" && INPUT_TOOLS=$((INPUT_TOOLS + 1))

echo ""
echo "📊 Tool Availability Summary:"
echo "   Screenshot tools: $SCREENSHOT_TOOLS available"
echo "   Display tools: $DISPLAY_TOOLS available"
echo "   Input simulation: $INPUT_TOOLS available"

# Check if we're in a graphical environment
if [[ -n "$DISPLAY" ]]; then
    echo "✅ X11 display available: $DISPLAY"
    X11_AVAILABLE=true
else
    echo "❌ No X11 display"
    X11_AVAILABLE=false
fi

if [[ -n "$WAYLAND_DISPLAY" ]]; then
    echo "✅ Wayland display available: $WAYLAND_DISPLAY"
    WAYLAND_AVAILABLE=true
else
    echo "❌ No Wayland display"
    WAYLAND_AVAILABLE=false
fi

echo ""

# Recommend the best approach based on available tools
echo "🎯 Recommended Testing Approach:"

if [[ $WAYLAND_AVAILABLE == true ]] && command -v grim >/dev/null 2>&1; then
    echo "1. ✅ BEST: Nested Wayland with grim screenshots"
    echo "   - Run compositor in nested Wayland session"
    echo "   - Use grim for native Wayland screenshots"
    echo "   - Command: WLR_BACKENDS=wayland ./build/fluxbox-wayland"
    BEST_METHOD="nested_wayland"
    
elif [[ $X11_AVAILABLE == true ]] && command -v scrot >/dev/null 2>&1; then
    echo "2. ✅ GOOD: X11 backend with scrot screenshots"
    echo "   - Run compositor with X11 backend"
    echo "   - Use scrot for X11 screenshots"
    echo "   - Command: WLR_BACKENDS=x11 ./build/fluxbox-wayland"
    BEST_METHOD="x11_backend"
    
elif command -v Xvfb >/dev/null 2>&1; then
    echo "3. ⚠️  OK: Xvfb virtual display"
    echo "   - Create virtual X11 display with Xvfb"
    echo "   - Run compositor with X11 backend"
    echo "   - Use available screenshot tools"
    BEST_METHOD="xvfb_virtual"
    
else
    echo "4. 📝 FALLBACK: Headless with simulated evidence"
    echo "   - Run in headless mode"
    echo "   - Generate visual evidence documents"
    echo "   - Create detailed text-based evidence"
    BEST_METHOD="headless_simulation"
fi

echo ""

# Create test scripts for each method
echo "📝 Creating test scripts for visual demonstration..."

# Method 1: Nested Wayland script
cat > run_visual_test_wayland.sh << 'EOF'
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
EOF

# Method 2: X11 backend script  
cat > run_visual_test_x11.sh << 'EOF'
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
EOF

# Method 3: Xvfb script
cat > run_visual_test_xvfb.sh << 'EOF'
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
EOF

# Method 4: Headless simulation script
cat > run_visual_test_headless.sh << 'EOF'
#!/bin/bash
# run_visual_test_headless.sh - Run visual tests with headless simulation

echo "🚀 Starting visual test with headless simulation..."

# Set environment for headless
export WLR_BACKENDS=headless
export WLR_RENDERER=pixman

echo "📱 Backend: Headless (simulation mode)"
echo "📱 Note: Will generate text-based visual evidence"

# Run visual demonstration
python3 test_visual_demonstration.py
EOF

# Make all scripts executable
chmod +x run_visual_test_*.sh

echo "✅ Created visual testing scripts:"
echo "   - run_visual_test_wayland.sh (nested Wayland)"
echo "   - run_visual_test_x11.sh (X11 backend)"
echo "   - run_visual_test_xvfb.sh (Xvfb virtual display)"
echo "   - run_visual_test_headless.sh (headless simulation)"

echo ""
echo "🎯 Recommended command for your system:"
case $BEST_METHOD in
    "nested_wayland")
        echo "   ./run_visual_test_wayland.sh"
        ;;
    "x11_backend")
        echo "   ./run_visual_test_x11.sh"
        ;;
    "xvfb_virtual")
        echo "   ./run_visual_test_xvfb.sh"
        ;;
    "headless_simulation")
        echo "   ./run_visual_test_headless.sh"
        ;;
esac

echo ""
echo "📋 To install missing tools (Arch Linux):"
if [[ $SCREENSHOT_TOOLS -eq 0 ]]; then
    echo "   sudo pacman -S grim scrot imagemagick"
fi
if [[ $DISPLAY_TOOLS -eq 0 ]]; then
    echo "   sudo pacman -S xorg-server-xvfb weston"
fi
if [[ $INPUT_TOOLS -eq 0 ]]; then
    echo "   sudo pacman -S wtype ydotool"
fi

echo ""
echo "🚀 Setup complete! Run the recommended script to capture visual evidence."
echo "============================================================"