#!/bin/bash
# GroveEngine - Run All Integration Tests
# Usage: ./run_all_tests.sh [options]
#
# Options:
#   --verbose    Show full test output
#   --parallel   Run tests in parallel (faster but harder to read)
#   --summary    Only show pass/fail summary
#   --continue   Continue running tests even if one fails

set -e

VERBOSE=false
PARALLEL=false
SUMMARY=false
CONTINUE=false
BUILD_DIR="build"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --parallel|-j)
            PARALLEL=true
            shift
            ;;
        --summary|-s)
            SUMMARY=true
            shift
            ;;
        --continue|-c)
            CONTINUE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --verbose, -v     Show full test output"
            echo "  --parallel, -j    Run tests in parallel"
            echo "  --summary, -s     Only show pass/fail summary"
            echo "  --continue, -c    Continue running tests even if one fails"
            echo "  --help, -h        Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory '$BUILD_DIR' not found"
    echo "Run 'cmake -B build && cmake --build build' first"
    exit 1
fi

cd "$BUILD_DIR"

echo "════════════════════════════════════════════════════════════════"
echo "  GroveEngine - Integration Test Suite"
echo "════════════════════════════════════════════════════════════════"
echo ""

# Run with CTest
if [ "$PARALLEL" = true ]; then
    echo "Running tests in parallel..."
    if [ "$VERBOSE" = true ]; then
        ctest -j$(nproc) --output-on-failure
    else
        ctest -j$(nproc)
    fi
elif [ "$SUMMARY" = true ]; then
    echo "Running tests (summary only)..."
    ctest --output-on-failure 2>&1 | grep -E "(Test|Passed|Failed|Total)"
elif [ "$VERBOSE" = true ]; then
    echo "Running tests (verbose mode)..."
    ctest --verbose
else
    echo "Running tests..."
    if [ "$CONTINUE" = true ]; then
        ctest --output-on-failure || true
    else
        ctest --output-on-failure
    fi
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "  Test Run Complete"
echo "════════════════════════════════════════════════════════════════"
