#!/bin/bash
# Script to install the latest LLVM/Clang from apt.llvm.org
# Prefers the qualification/RC branch, falls back to stable if not available
#
# LLVM provides three branches:
#   - Stable: Production-ready release
#   - Qualification: Release candidate / testing
#   - Development: Nightly/trunk builds
#
# Usage: ./install-latest-llvm.sh
# Output: Sets CC and CXX environment variables in $GITHUB_ENV

set -e

echo "=== Installing latest LLVM/Clang from apt.llvm.org ==="

# Download the official LLVM installation script
wget -q https://apt.llvm.org/llvm.sh
chmod +x llvm.sh

# Get the Ubuntu codename (e.g., noble, jammy)
CODENAME=$(lsb_release -cs)
echo "Ubuntu codename: ${CODENAME}"

# Parse the LLVM apt page to find available versions for this codename
# The page lists llvm-toolchain-<codename>-<version> packages
AVAILABLE_VERSIONS=$(curl -s --compressed https://apt.llvm.org/ | grep -o "llvm-toolchain-${CODENAME}-[0-9]*" | sed "s/llvm-toolchain-${CODENAME}-//" | sort -n | uniq)

if [ -z "$AVAILABLE_VERSIONS" ]; then
    echo "Warning: Could not parse available versions from apt.llvm.org"
    AVAILABLE_VERSIONS=""
fi

echo "Available LLVM versions for ${CODENAME}: ${AVAILABLE_VERSIONS:-none found}"

# Get the stable version from the llvm.sh script
STABLE_VERSION=$(grep 'CURRENT_LLVM_STABLE=' llvm.sh | head -1 | sed 's/.*CURRENT_LLVM_STABLE=//')
echo "Stable version (from llvm.sh): ${STABLE_VERSION}"

# Calculate the qualification version (one above stable)
QUALIFICATION_VERSION=$((STABLE_VERSION + 1))
echo "Qualification version: ${QUALIFICATION_VERSION}"

# Determine which version to install
# Prefer qualification branch, fall back to stable
if echo "$AVAILABLE_VERSIONS" | grep -q "^${QUALIFICATION_VERSION}$"; then
    CLANG_VERSION=$QUALIFICATION_VERSION
    echo "==> Using qualification/RC branch: clang-${CLANG_VERSION}"
else
    CLANG_VERSION=$STABLE_VERSION
    echo "==> Qualification branch not available, using stable branch: clang-${CLANG_VERSION}"
fi

# Install the selected version with all tools
echo "Installing clang-${CLANG_VERSION}..."
sudo ./llvm.sh $CLANG_VERSION all

# Verify installation
echo "Verifying installation..."
clang-${CLANG_VERSION} --version

# Set environment variables for GitHub Actions
if [ -n "$GITHUB_ENV" ]; then
    echo "CC=clang-$CLANG_VERSION" >> $GITHUB_ENV
    echo "CXX=clang++-$CLANG_VERSION" >> $GITHUB_ENV
    echo "Environment variables CC and CXX set in GITHUB_ENV"
else
    # For local testing, export variables
    export CC=clang-$CLANG_VERSION
    export CXX=clang++-$CLANG_VERSION
    echo "Environment variables exported: CC=$CC, CXX=$CXX"
fi

echo "=== LLVM/Clang installation complete ==="