#!/bin/bash
# Test script to verify disable-mainnet flag is set correctly based on version type

echo "Testing disable-mainnet flag logic..."
echo ""

# Test 1: Snapshot version (develop branch)
echo "Test 1: Develop branch (should have --disable-mainnet)"
GITHUB_EVENT_NAME="push"
GITHUB_REF="refs/heads/develop"
disable_mainnet=""

if [[ "$GITHUB_EVENT_NAME" == "pull_request" ]] || [[ "$GITHUB_REF" == *develop ]] || [[ "$GITHUB_REF" == *ft/* ]] || [[ "$GITHUB_REF" == *bug/* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == *"release/"* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == "refs/heads/master" ]]; then
    disable_mainnet=""
fi

if [[ "$disable_mainnet" == "--disable-mainnet" ]]; then
    echo "✓ PASS: develop branch sets --disable-mainnet"
else
    echo "✗ FAIL: develop branch should set --disable-mainnet"
fi
echo ""

# Test 2: Release candidate (release/ branch)
echo "Test 2: Release branch (should have --disable-mainnet)"
GITHUB_EVENT_NAME="push"
GITHUB_REF="refs/heads/release/2.0.3.04"
disable_mainnet=""

if [[ "$GITHUB_EVENT_NAME" == "pull_request" ]] || [[ "$GITHUB_REF" == *develop ]] || [[ "$GITHUB_REF" == *ft/* ]] || [[ "$GITHUB_REF" == *bug/* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == *"release/"* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == "refs/heads/master" ]]; then
    disable_mainnet=""
fi

if [[ "$disable_mainnet" == "--disable-mainnet" ]]; then
    echo "✓ PASS: release branch sets --disable-mainnet"
else
    echo "✗ FAIL: release branch should set --disable-mainnet"
fi
echo ""

# Test 3: Production release (master branch)
echo "Test 3: Master branch (should NOT have --disable-mainnet)"
GITHUB_EVENT_NAME="push"
GITHUB_REF="refs/heads/master"
disable_mainnet=""

if [[ "$GITHUB_EVENT_NAME" == "pull_request" ]] || [[ "$GITHUB_REF" == *develop ]] || [[ "$GITHUB_REF" == *ft/* ]] || [[ "$GITHUB_REF" == *bug/* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == *"release/"* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == "refs/heads/master" ]]; then
    disable_mainnet=""
fi

if [[ "$disable_mainnet" == "" ]]; then
    echo "✓ PASS: master branch does NOT set --disable-mainnet"
else
    echo "✗ FAIL: master branch should NOT set --disable-mainnet"
fi
echo ""

# Test 4: Feature branch
echo "Test 4: Feature branch (should have --disable-mainnet)"
GITHUB_EVENT_NAME="push"
GITHUB_REF="refs/heads/ft/new-feature"
disable_mainnet=""

if [[ "$GITHUB_EVENT_NAME" == "pull_request" ]] || [[ "$GITHUB_REF" == *develop ]] || [[ "$GITHUB_REF" == *ft/* ]] || [[ "$GITHUB_REF" == *bug/* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == *"release/"* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == "refs/heads/master" ]]; then
    disable_mainnet=""
fi

if [[ "$disable_mainnet" == "--disable-mainnet" ]]; then
    echo "✓ PASS: feature branch sets --disable-mainnet"
else
    echo "✗ FAIL: feature branch should set --disable-mainnet"
fi
echo ""

# Test 5: Pull request
echo "Test 5: Pull request (should have --disable-mainnet)"
GITHUB_EVENT_NAME="pull_request"
GITHUB_REF="refs/heads/master"
disable_mainnet=""

if [[ "$GITHUB_EVENT_NAME" == "pull_request" ]] || [[ "$GITHUB_REF" == *develop ]] || [[ "$GITHUB_REF" == *ft/* ]] || [[ "$GITHUB_REF" == *bug/* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == *"release/"* ]]; then
    disable_mainnet="--disable-mainnet"
elif [[ "$GITHUB_EVENT_NAME" != "pull_request" ]] && [[ "$GITHUB_REF" == "refs/heads/master" ]]; then
    disable_mainnet=""
fi

if [[ "$disable_mainnet" == "--disable-mainnet" ]]; then
    echo "✓ PASS: pull request sets --disable-mainnet"
else
    echo "✗ FAIL: pull request should set --disable-mainnet"
fi
echo ""

echo "All tests completed!"

