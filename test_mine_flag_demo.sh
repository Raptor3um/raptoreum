#!/bin/bash

echo "=== Raptoreum listassets mine flag demo ==="
echo ""

echo "1. Show all assets (mine=false, default):"
echo "   raptoreum-cli listassets"
echo "   Result: Shows all assets in the network"
echo ""

echo "2. Show only wallet assets (mine=true):"
echo "   raptoreum-cli listassets false 100 0 true"
echo "   Result: Shows only assets owned by current wallet"
echo ""

echo "3. Verbose mode with wallet filter:"
echo "   raptoreum-cli listassets true 50 0 true"
echo "   Result: Shows detailed info for wallet-owned assets only"
echo ""

echo "=== Parameter breakdown ==="
echo "listassets [verbose] [count] [start] [mine]"
echo "  verbose: false/true (default: false)"
echo "  count: number of assets to return (default: all)"
echo "  start: starting position (default: 0)"
echo "  mine: true/false/1/0/'true'/'false' (default: false)"
echo ""
echo "The new 'mine' parameter filters results to show only"
echo "assets that belong to the current wallet when set to true."
