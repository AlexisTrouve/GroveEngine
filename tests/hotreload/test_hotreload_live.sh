#!/bin/bash

# Test script for live hot-reload during engine execution
# This script validates that hot-reload works while the engine is running

set -e

echo "========================================"
echo "🔥 LIVE HOT-RELOAD TEST"
echo "========================================"
echo "This test will:"
echo "  1. Start the engine"
echo "  2. Modify TestModule.cpp while running"
echo "  3. Recompile the module"
echo "  4. Verify hot-reload happens automatically"
echo "========================================"
echo ""

# Configuration
BUILD_DIR="../../build"
TEST_DIR="$BUILD_DIR/tests"
MODULE_SOURCE="../../tests/modules/TestModule.cpp"
ENGINE_EXEC="$TEST_DIR/test_engine_hotreload"
LOG_FILE="/tmp/grove_hotreload_test.log"

# Check that we're in the right directory
if [ ! -f "$ENGINE_EXEC" ]; then
    echo "❌ Error: test_engine_hotreload not found at $ENGINE_EXEC"
    echo "Please run this script from tests/hotreload/ directory"
    exit 1
fi

# Clean previous log
rm -f "$LOG_FILE"

# Step 1: Start the engine in background
echo "🚀 Step 1/5: Starting engine in background..."
cd "$TEST_DIR"
./test_engine_hotreload > "$LOG_FILE" 2>&1 &
ENGINE_PID=$!
echo "   Engine PID: $ENGINE_PID"
echo "   Log file: $LOG_FILE"

# Give engine time to fully start
echo "⏳ Waiting 3 seconds for engine to start..."
sleep 3

# Check if engine is still running
if ! kill -0 $ENGINE_PID 2>/dev/null; then
    echo "❌ Engine died during startup!"
    cat "$LOG_FILE"
    exit 1
fi

echo "✅ Engine is running"
echo ""

# Step 2: Check initial state
echo "📊 Step 2/5: Checking initial state..."
INITIAL_COUNT=$(grep -c "Version: v1.0" "$LOG_FILE" || echo "0")
echo "   Initial frames processed with v1.0: $INITIAL_COUNT"

if [ "$INITIAL_COUNT" -lt 10 ]; then
    echo "❌ Engine not processing frames properly (expected >= 10, got $INITIAL_COUNT)"
    kill $ENGINE_PID 2>/dev/null || true
    exit 1
fi

echo "✅ Engine processing frames normally"
echo ""

# Step 3: Modify TestModule.cpp
echo "🔧 Step 3/5: Modifying TestModule.cpp..."
TEMP_MODULE="/tmp/TestModule_backup.cpp"
cp "$MODULE_SOURCE" "$TEMP_MODULE"

# Change version from v1.0 to v2.0 HOT-RELOADED!
sed -i 's/v1\.0/v2.0 HOT-RELOADED!/g' "$MODULE_SOURCE"

echo "✅ TestModule.cpp modified (v1.0 → v2.0 HOT-RELOADED!)"
echo ""

# Step 4: Recompile module
echo "🔨 Step 4/5: Recompiling TestModule..."
cd "$BUILD_DIR"
make TestModule > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "❌ Compilation failed!"
    # Restore original
    cp "$TEMP_MODULE" "$MODULE_SOURCE"
    kill $ENGINE_PID 2>/dev/null || true
    exit 1
fi

echo "✅ TestModule recompiled successfully"
echo ""

# Step 5: Wait and verify hot-reload happened
echo "⏳ Step 5/5: Waiting for hot-reload detection..."
sleep 2

# Check if hot-reload was triggered
if grep -q "Hot-reload completed" "$LOG_FILE"; then
    echo "✅ Hot-reload was triggered!"

    # Check if new version is running
    if grep -q "v2.0 HOT-RELOADED!" "$LOG_FILE"; then
        echo "✅ New version (v2.0) is running!"

        # Count frames with new version
        V2_COUNT=$(grep -c "v2.0 HOT-RELOADED!" "$LOG_FILE" || echo "0")
        echo "   Frames processed with v2.0: $V2_COUNT"

        if [ "$V2_COUNT" -ge 5 ]; then
            echo ""
            echo "========================================"
            echo "🎉 HOT-RELOAD TEST PASSED!"
            echo "========================================"
            echo "✅ Engine ran with v1.0"
            echo "✅ Module was modified and recompiled"
            echo "✅ Hot-reload was detected and executed"
            echo "✅ Engine continued running with v2.0"
            echo "========================================"

            # Show reload timing
            RELOAD_TIME=$(grep "Hot-reload completed in" "$LOG_FILE" | tail -1 | grep -oP '\d+\.\d+(?=ms)')
            if [ -n "$RELOAD_TIME" ]; then
                echo "⚡ Hot-reload time: ${RELOAD_TIME}ms"
            fi

            SUCCESS=true
        else
            echo "❌ Not enough frames with v2.0 (expected >= 5, got $V2_COUNT)"
            SUCCESS=false
        fi
    else
        echo "❌ New version not detected in output"
        SUCCESS=false
    fi
else
    echo "❌ Hot-reload was not triggered"
    echo ""
    echo "Last 30 lines of log:"
    tail -30 "$LOG_FILE"
    SUCCESS=false
fi

# Cleanup
echo ""
echo "🧹 Cleaning up..."
kill $ENGINE_PID 2>/dev/null || true
wait $ENGINE_PID 2>/dev/null || true

# Restore original module
cp "$TEMP_MODULE" "$MODULE_SOURCE"
rm -f "$TEMP_MODULE"

# Recompile original version
echo "🔄 Restoring original TestModule..."
cd "$BUILD_DIR"
make TestModule > /dev/null 2>&1

if [ "$SUCCESS" = true ]; then
    echo "✅ Test completed successfully"
    exit 0
else
    echo "❌ Test failed"
    exit 1
fi
