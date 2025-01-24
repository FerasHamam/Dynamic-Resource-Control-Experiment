#!/bin/bash
# Function to add a tc class and filter
function add_tc_rule() {
    local interface="$1"
    local ip_addr="$2"
    local port_1="$3"
    local port_2="$4"

    # High-priority filter
    echo "Adding filter for high-priority port $port_1."
    sudo tc filter add dev "$interface" protocol ip parent 1: u32 \
        match ip dst "$ip_addr" match ip dport "$port_1" 0xffff flowid 1:1 || {
        echo "Failed to add filter for high-priority port $port_1."
        return 1
    }

    # Low-priority filter
    echo "Adding filter for low-priority port $port_2."
    sudo tc filter add dev "$interface" protocol ip parent 1: u32 \
        match ip dst "$ip_addr" match ip dport "$port_2" 0xffff flowid 1:2 || {
        echo "Failed to add filter for low-priority port $port_2."
        return 1
    }
}

# Ensure the script is called with proper arguments
if [[ $# -ne 4 ]]; then
    echo "Usage: $0 <interface> <ip_address> <port_1> <port_2>"
    exit 1
fi

# Check if tc command is available
if ! command -v tc &> /dev/null; then
    echo "tc command not found. Please install iproute2 package."
    exit 1
fi

# Extract arguments
interface="$1"
ip_addr="$2"
port_1="$3"
port_2="$4"

add_tc_rule "$interface" "$ip_addr" "$port_1" "$port_2"
if [[ $? -ne 0 ]]; then
    echo "Failed to add tc rules."
    exit 1
fi

echo "tc rules added successfully."
