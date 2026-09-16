#!/usr/bin/env bash

set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly APP_DIR

if (($# < 2 || $# > 5)); then
    echo "Usage: $0 EXP_ID NODES_FILE [NTX] [SITE] [BOARD]" >&2
    exit 1
fi

EXP_ID="$1"
NODES_FILE="$2"
NTX="${3:-24}"
SITE="${4:-toulouse}"
BOARD="${5:-dwm1001}"

if ! [[ "${EXP_ID}" =~ ^[1-9][0-9]*$ ]]; then
    echo "EXP_ID must be a positive integer" >&2
    exit 1
fi

if ! [[ "${NTX}" =~ ^[1-9][0-9]*$ ]]; then
    echo "NTX must be a positive integer" >&2
    exit 1
fi

if [[ ! -f "${NODES_FILE}" ]]; then
    echo "Node file not found: ${NODES_FILE}" >&2
    exit 1
fi

if ! command -v iotlab-node >/dev/null; then
    echo "iotlab-node was not found in PATH" >&2
    exit 1
fi

declare -a nodes=()
declare -A seen_nodes=()

while IFS= read -r line || [[ -n "${line}" ]]; do
    line="${line%%#*}"
    read -ra line_nodes <<< "${line}"

    for node in "${line_nodes[@]}"; do
        if ! [[ "${node}" =~ ^[1-9][0-9]*$ ]]; then
            echo "Invalid physical node ID: ${node}" >&2
            exit 1
        fi
        if [[ -n "${seen_nodes[${node}]:-}" ]]; then
            echo "Duplicate physical node ID: ${node}" >&2
            exit 1
        fi

        nodes+=("${node}")
        seen_nodes["${node}"]=1
    done
done < "${NODES_FILE}"

count="${#nodes[@]}"
if ((count == 0)); then
    echo "No physical node IDs found in ${NODES_FILE}" >&2
    exit 1
fi

OUT="${OUT:-${APP_DIR}/firmwares/n${count}-x${NTX}}"

for logical_id in "${!nodes[@]}"; do
    firmware="${OUT}/id-${logical_id}/${BOARD}/timecast.elf"
    if [[ ! -f "${firmware}" ]]; then
        echo "Firmware not found: ${firmware}" >&2
        exit 1
    fi
    printf 'logical %d -> %s-%s\n' \
        "${logical_id}" "${BOARD}" "${nodes[${logical_id}]}"
done

node_list="$(IFS=+; echo "${nodes[*]}")"
selection="${SITE},${BOARD},${node_list}"

echo "Experiment: ${EXP_ID}"
echo "Firmware directory: ${OUT}"
echo "Nodes: ${selection}"

if [[ "${YES:-0}" != 1 ]]; then
    read -r -p "Flash ${count} nodes? [y/N] " reply
    if ! [[ "${reply}" =~ ^[Yy]$ ]]; then
        echo "Cancelled"
        exit 0
    fi
fi


for logical_id in "${!nodes[@]}"; do
    physical_id="${nodes[${logical_id}]}"
    firmware="${OUT}/id-${logical_id}/${BOARD}/timecast.elf"

    echo "Flashing logical ${logical_id} on ${BOARD}-${physical_id}"
    iotlab-node -i "${EXP_ID}" --flash "${firmware}" \
        -l "${SITE},${BOARD},${physical_id}"
done

iotlab-node -i "${EXP_ID}" --reset -l "${selection}"

echo "Flashed and started ${count} nodes"
