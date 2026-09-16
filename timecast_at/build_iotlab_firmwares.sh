#!/usr/bin/env bash

set -euo pipefail

readonly APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

COUNT="${1:-40}"
NTX="${2:-10}"
BOARD="${3:-dwm1001}"
JOBS="${JOBS:-4}"
OUT="${OUT:-${APP_DIR}/firmwares/n${COUNT}-x${NTX}}"

if ! [[ "${COUNT}" =~ ^[1-9][0-9]*$ ]]; then
    echo "COUNT must be a positive integer" >&2
    exit 1
fi

if ! [[ "${NTX}" =~ ^[1-9][0-9]*$ ]]; then
    echo "NTX must be a positive integer" >&2
    exit 1
fi

for ((id = 0; id < COUNT; id++)); do
    echo "Building LOCAL_NODE_ID=${id}"
    make -j"${JOBS}" all \
        BOARD="${BOARD}" \
        LOCAL_NODE_ID="${id}" \
        TIMECAST_P2_NODE_COUNT="${COUNT}" \
        TIMECAST_P2_NTX="${NTX}" \
        BINDIRBASE="${OUT}/id-${id}"
done

firmware_count="$(find "${OUT}" -type f -name timecast.elf | wc -l)"
if ((firmware_count != COUNT)); then
    echo "Expected ${COUNT} firmware files, found ${firmware_count}" >&2
    exit 1
fi

echo "Built ${firmware_count} firmware files in ${OUT}"
