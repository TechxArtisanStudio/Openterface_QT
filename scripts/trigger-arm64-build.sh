#!/bin/bash

# Helper script to trigger ARM64 build pipeline via GitHub CLI
# Requires: gh CLI tool (https://cli.github.com/)

set -e

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    echo "❌ GitHub CLI (gh) is not installed."
    echo "📦 Install it from: https://cli.github.com/"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    echo "❌ Not authenticated with GitHub CLI."
    echo "🔑 Run: gh auth login"
    exit 1
fi

echo "🚀 ARM64 Build Pipeline Trigger"
echo "================================"

# Get current branch
CURRENT_BRANCH=$(git branch --show-current)
echo "📂 Current branch: $CURRENT_BRANCH"

# Input parameters
read -p "Qt Version (default: 6.5.3): " QT_VERSION
QT_VERSION=${QT_VERSION:-6.5.3}

read -p "Rebuild base environment? (y/N): " -n 1 -r REBUILD_BASE
echo
if [[ $REBUILD_BASE =~ ^[Yy]$ ]]; then
    REBUILD_BASE="true"
else
    REBUILD_BASE="false"
fi

read -p "Rebuild Qt environment? (y/N): " -n 1 -r REBUILD_QT
echo
if [[ $REBUILD_QT =~ ^[Yy]$ ]]; then
    REBUILD_QT="true"
else
    REBUILD_QT="false"
fi

echo ""
echo "📋 Pipeline Configuration:"
echo "   Qt Version: $QT_VERSION"
echo "   Rebuild Base: $REBUILD_BASE"
echo "   Rebuild Qt: $REBUILD_QT"
echo ""

read -p "Trigger the pipeline? (y/N): " -n 1 -r CONFIRM
echo

if [[ $CONFIRM =~ ^[Yy]$ ]]; then
    echo "🔄 Triggering ARM64 build pipeline..."
    
    gh workflow run arm64-pipeline.yml \
        --field qt_version="$QT_VERSION" \
        --field rebuild_base="$REBUILD_BASE" \
        --field rebuild_qt="$REBUILD_QT"
    
    echo "✅ Pipeline triggered successfully!"
    echo "🔗 Monitor progress at: https://github.com/$(gh repo view --json owner,name -q '.owner.login + "/" + .name')/actions"
    
    # Optional: wait and show status
    read -p "Watch the pipeline progress? (y/N): " -n 1 -r WATCH
    echo
    if [[ $WATCH =~ ^[Yy]$ ]]; then
        echo "👀 Watching pipeline (Ctrl+C to stop)..."
        gh run watch --exit-status
    fi
else
    echo "❌ Pipeline trigger cancelled."
fi
