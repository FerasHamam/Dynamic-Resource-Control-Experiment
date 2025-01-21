#!/bin/bash

# Function to add a tc class and filter
function add_tc_rule() {
    local interface="$1"
    local ip_addr="$2"
    local base_port="$3"
    local priority="$4"

    local port=$base_port
    if [ "$priority" == "high" ]; then
        # High-priority filter
        echo "Adding filter for high-priority port $port."
        sudo tc filter add dev "$interface" protocol ip parent 1: u32 \
            match ip dst "$ip_addr" match ip dport "$port" 0xffff flowid 1:1 || {
            echo "Failed to add filter for high-priority port $port."
            return 1
        }
    elif [ "$priority" == "low" ]; then
        # Low-priority filter
        echo "Adding filter for low-priority port $port."
        sudo tc filter add dev "$interface" protocol ip parent 1: u32 \
            match ip dst "$ip_addr" match ip dport "$port" 0xffff flowid 1:2 || {
            echo "Failed to add filter for low-priority port $port."
            return 1
        }
    else
        echo "Invalid priority: $priority. Must be 'high' or 'low'."
        return 1
    fi
}

# Ensure the script is called with proper arguments
if [[ $# -ne 4 ]]; then
    echo "Usage: $0 <interface> <ip_address> <base_port> <priority>"
    exit 1
fi

# Extract arguments
interface="$1"
ip_addr="$2"
base_port="$3"
priority="$4"

# Check if priority is valid
if [[ "$priority" != "high" && "$priority" != "low" ]]; then
    echo "Invalid priority: $priority. Must be 'high' or 'low'."
    exit 1
fi

add_tc_rule "$interface" "$ip_addr" "$base_port" "$priority"
