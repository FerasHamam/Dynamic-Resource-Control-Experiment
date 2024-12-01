#!/bin/bash

LOCKFILE="/var/lock/controlNetPrio.lock"

# Function to add a tc class and filter
function add_tc_rule() {
    local interface="$1"
    local ip_addr="$2"
    local base_port="$3"
    local num_ports="$4"
    local high_bw="100mbit" # Bandwidth for high-priority class
    local low_bw="30mbit"  # Bandwidth for low-priority classes

    # Add root qdisc if it doesn't exist
    if ! tc qdisc show dev "$interface" | grep -q "htb 1:"; then
        echo "Adding root qdisc for interface $interface."
        sudo tc qdisc add dev "$interface" root handle 1: htb || {
            echo "Failed to add root qdisc for interface $interface."
            return 1
        }
    else
        echo "Root qdisc already exists for interface $interface."
    fi

    # Add high-priority class if it doesn't already exist
    if ! tc class show dev "$interface" | grep -q "class htb 1:1"; then
        echo "Adding high-priority class 1:1."
        sudo tc class add dev "$interface" parent 1: classid 1:1 htb rate "$high_bw" ceil "$high_bw" || {
            echo "Failed to add high-priority class for interface $interface."
            return 1
        }
    else
        echo "High-priority class 1:1 already exists for interface $interface."
    fi

    # Add low-priority class if it doesn't already exist
    if ! tc class show dev "$interface" | grep -q "class htb 1:2"; then
        echo "Adding low-priority class 1:2."
        sudo tc class add dev "$interface" parent 1: classid 1:2 htb rate "$low_bw" ceil "$low_bw" || {
            echo "Failed to add low-priority class for interface $interface."
            return 1
        }
    else
        echo "Low-priority class 1:2 already exists for interface $interface."
    fi

    # Add filters for each port dynamically
    for ((i=0; i<num_ports; i++)); do
        local port=$((base_port + i))
        if [ $i -eq 0 ]; then
            # High-priority filter for the first port
            echo "Adding filter for high-priority port $port."
            sudo tc filter add dev "$interface" protocol ip parent 1: u32 \
                match ip dst "$ip_addr" match ip dport "$port" 0xffff flowid 1:1 || {
                echo "Failed to add filter for high-priority port $port."
                return 1
            }
        else
            # Low-priority filters for the remaining ports
            echo "Adding filter for low-priority port $port."
            sudo tc filter add dev "$interface" protocol ip parent 1: u32 \
                match ip dst "$ip_addr" match ip dport "$port" 0xffff flowid 1:2 || {
                echo "Failed to add filter for low-priority port $port."
                return 1
            }
        fi
    done
}

# Ensure the script is called with proper arguments
if [[ $# -ne 4 ]]; then
    echo "Usage: $0 <interface> <ip_address> <base_port> <num_ports>"
    exit 1
fi

# Extract arguments
interface="$1"
ip_addr="$2"
base_port="$3"
num_ports="$4"

# Serialize access using a lock
(
    flock -x 200 || { echo "Failed to acquire lock. Another instance might be running."; exit 1; }

    echo "Applying TC rules: interface=$interface, IP=$ip_addr, base_port=$base_port, num_ports=$num_ports"
    add_tc_rule "$interface" "$ip_addr" "$base_port" "$num_ports"

) 200>"$LOCKFILE"
