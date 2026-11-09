#!/bin/sh
set -eu
cmake -S "$(dirname "$0")" -B "$(dirname "$0")/build" -DIN_BUILD_FRONTEND=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$(dirname "$0")/build" -j 2
