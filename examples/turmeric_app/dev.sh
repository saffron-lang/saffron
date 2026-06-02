#!/bin/bash
# Dev server for the Turmeric demo app
# Watches build/ and src/ for changes, auto-reloads browser
set -e
cd "$(dirname "$0")"

PORT=${1:-8080}

echo "Starting Turmeric dev server..."
echo "  Build dir: build/"
echo "  Watch: build/ src/"
echo ""

node ../../turmeric/tools/dev_server.js build --port "$PORT" --watch build --watch src
