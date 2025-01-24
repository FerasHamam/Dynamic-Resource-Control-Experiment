#!/bin/bash
add_classes() {
    local interface="$1"
    local bw_1="$2"
    local bw_2="$3"

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

    # Add Reduced class
    if ! tc class show dev "$interface" | grep -q "class htb 1:1"; then
        echo "Adding class 1:1."
        sudo tc class add dev "$interface" parent 1: classid 1:1 htb rate "$bw_1" ceil "$bw_1" || {
            echo "Failed to add class for interface $interface."
            return 1
        }
    else
        echo "class 1:1 already exists for interface $interface. Changing its rate and ceil."
        sudo tc class change dev "$interface" parent 1: classid 1:1 htb rate "$bw_1" ceil "$bw_1" || {
            echo "Failed to change class for interface $interface."
            return 1
        }
    fi

    # Add Aug class
    if ! tc class show dev "$interface" | grep -q "class htb 1:2"; then
        echo "Adding class 1:2."
        sudo tc class add dev "$interface" parent 1: classid 1:2 htb rate "$bw_2" ceil "$bw_2" || {
            echo "Failed to add class for interface $interface."
            return 1
        }
    else
        echo "class 1:2 already exists for interface $interface. Changing its rate and ceil."
        sudo tc class change dev "$interface" parent 1: classid 1:2 htb rate "$bw_2" ceil "$bw_2" || {
            echo "Failed to change class for interface $interface."
            return 1
        }
    fi
}

# Ensure the script is called with proper arguments
if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <interface> <bw_1> <bw_2>"
    exit 1
fi

interface="$1"
bw_1="$2"
bw_2="$3"
add_classes "$interface" "$bw_1" "$bw_2"
