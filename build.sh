#!/bin/bash
# Build script wrapper - delegates to scripts/build.sh
exec "$(dirname "$0")/scripts/build.sh" "$@"
