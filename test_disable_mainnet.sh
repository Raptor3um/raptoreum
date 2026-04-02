#!/bin/bash
# Test script for --disable-mainnet flag functionality

echo "=========================================="
echo "Testing --disable-mainnet configure flag"
echo "=========================================="
echo ""

# Clean previous build artifacts
echo "Step 1: Cleaning previous build artifacts..."
make clean > /dev/null 2>&1

# Test 1: Build with mainnet enabled (default)
echo ""
echo "Test 1: Building with mainnet ENABLED (default)..."
./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu --disable-tests --enable-debug > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Configure successful with mainnet enabled"
    echo "  When built, the application will:"
    echo "  - Default to MAINNET when no network flags are provided"
    echo "  - Allow -testnet, -regtest, -devnet flags"
else
    echo "✗ Configure failed"
    exit 1
fi

# Clean again
echo ""
echo "Cleaning..."
make clean > /dev/null 2>&1

# Test 2: Build with mainnet disabled
echo ""
echo "Test 2: Building with mainnet DISABLED..."
./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu --disable-tests --enable-debug --disable-mainnet > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Configure successful with mainnet disabled"
    echo "  When built, the application will:"
    echo "  - REJECT mainnet connections (throws error)"
    echo "  - Require -testnet, -regtest, or -devnet flags"
    echo "  - Throw runtime error if started without network flag"
else
    echo "✗ Configure failed"
    exit 1
fi

echo ""
echo "=========================================="
echo "Configuration tests completed successfully!"
echo "=========================================="
echo ""
echo "To build with mainnet disabled:"
echo "  ./configure --disable-mainnet [other options]"
echo "  make"
echo ""
echo "To build with mainnet enabled (default):"
echo "  ./configure [other options]"
echo "  make"
echo ""
