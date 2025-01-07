#!/bin/bash
add_classes() {
    local interface="$1"
    local high_bw="100mbit"
    local low_bw="30mbit"

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
}

# Ensure the script is called with proper arguments
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <interface>"
    exit 1
fi

interface="$1"
add_classes "$interface"
