#!/usr/bin/env bash

#
# Copyright (c) 2020 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e

# Paths and environment
CHIP_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$CHIP_ROOT/out/$BUILD_TYPE"
CHECK_ALL=false

source "$CHIP_ROOT/scripts/activate.sh"

# Enable dmalloc if configured
dmalloc=$(gn --root="$CHIP_ROOT" args "$BUILD_DIR" --short --list=chip_config_memory_debug_dmalloc)

case "$dmalloc" in
    "chip_config_memory_debug_dmalloc = true")
        eval "$(dmalloc -b -i 1 high)"
        export G_SLICE=always-malloc
        ;;
    "chip_config_memory_debug_dmalloc = false") ;;
    *) echo >&2 "Invalid dmalloc output: \"$dmalloc\""; exit 1 ;;
esac

echo "🔍 Detecting if this is a PR build with file diff support..."

# Only try differential logic if in PR context
if [[ -n "$GITHUB_EVENT_NAME" && "$GITHUB_EVENT_NAME" == "pull_request" && -n "$GITHUB_BASE_REF" ]]; then
    echo "📂 Fetching changed files..."
    git fetch origin "$GITHUB_BASE_REF" --depth=1 || true
    CHANGED_FILES=$(git diff --name-only origin/"$GITHUB_BASE_REF"...HEAD)

    echo "🔍 Changed files:"
    echo "$CHANGED_FILES"

    TESTS_TO_RUN=()

    # Try to map source files to tests
    for f in $CHANGED_FILES; do
        base=$(basename "$f")
        test_bin="${BUILD_DIR}/tests/test_${base%.*}"

        if [[ -x "$test_bin" ]]; then
            TESTS_TO_RUN+=("$test_bin")
        fi
    done

    if [ ${#TESTS_TO_RUN[@]} -eq 0 ]; then
        echo "⚠️ No matching test binaries found for changed files. Running all tests instead."
        CHECK_ALL=true
    fi
else
    echo "ℹ️ Not a PR build or missing base ref — running all tests."
    CHECK_ALL=true
fi

set -x

if [ "$CHECK_ALL" = true ]; then
    ninja -v -C "$BUILD_DIR" -k 0 check
else
    echo "✅ Running selected test(s) only:"
    for t in "${TESTS_TO_RUN[@]}"; do
        echo "▶️  $t"
        "$t"
    done
fi

