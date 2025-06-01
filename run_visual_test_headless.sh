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
