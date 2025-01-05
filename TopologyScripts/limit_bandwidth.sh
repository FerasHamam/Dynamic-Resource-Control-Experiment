#!/bin/bash

apply_bandwidth_limit() {
    local interface="$1"
    local bandwidth="$2"  # The bandwidth limit is passed as an argument

    # Remove any previous configurations
    sudo tc qdisc del dev "$interface" root 2>/dev/null
    
    # Add root qdisc for egress traffic
    sudo tc qdisc add dev "$interface" root handle 1: htb || return 1

    # Add class for egress traffic
    sudo tc class add dev "$interface" parent 1: classid 1:1 htb rate "$bandwidth" ceil "$bandwidth" || return 1

    # Add filter to match all outgoing traffic
    sudo tc filter add dev "$interface" protocol ip parent 1:0 u32 match ip dst 0.0.0.0/0 flowid 1:1 || return 1
}

list_interfaces() {
    ip -o link show | awk -F': ' '{print $2}'
}

# Check if the correct number of arguments is provided
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <bandwidth>"
    echo "Example: $0 200mbit"
    exit 1
fi

bandwidth="$1"
echo "Available network interfaces:"
interfaces=($(list_interfaces))

# Display a numbered list with a clearer prompt for selection
echo "Please choose an interface to apply the $bandwidth limit (enter the corresponding number):"
select interface in "${interfaces[@]}"; do
    if [[ -n "$interface" ]]; then
        echo "You selected: $interface"
        apply_bandwidth_limit "$interface" "$bandwidth"
        break
    else
        echo "Invalid selection. Please choose a valid number from the list."
    fi
done