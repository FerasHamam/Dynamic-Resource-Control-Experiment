#!/bin/bash

apply_bandwidth_limit() {
    local interface="$1"
    local bandwidth="$2"

    # Remove any previous configurations
    sudo tc qdisc del dev "$interface" root 2>/dev/null

    # Add root qdisc for egress(output) traffic
    sudo tc qdisc add dev "$interface" root handle 1: htb || return 1

    # Add class for egress traffic
    sudo tc class add dev "$interface" parent 1: classid 1:1 htb rate "$bandwidth" ceil "$bandwidth" || return 1

    # Add filter to match all outgoing traffic
    sudo tc filter add dev "$interface" protocol ip parent 1:0 u32 match ip dst 0.0.0.0/0 flowid 1:1 || return 1
}

get_mac_address() {
    local interface="$1"
    cat "/sys/class/net/$interface/address" 2>/dev/null
}

list_interfaces() {
    interfaces=()
    for iface in $(ip -o link show | awk -F': ' '{print $2}'); do
        mac=$(get_mac_address "$iface")
        if [[ -n "$mac" ]]; then
            interfaces+=("$iface-$mac")
        fi
    done
    echo "${interfaces[@]}"
}

# Check if the correct number of arguments is provided
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <bandwidth>"
    echo "Example: $0 200mbit"
    exit 1
fi

bandwidth="$1"

interfaces=($(list_interfaces))

# Display a numbered list with interface name and MAC address for selection
echo "Available network interfaces (name-MAC address):"
select iface_info in "${interfaces[@]}"; do
    if [[ -n "$iface_info" ]]; then
        selected_interface=$(echo "$iface_info" | awk -F'-' '{print $1}')
        echo "You selected: $iface_info"
        echo "$selected_interface"
        apply_bandwidth_limit "$selected_interface" "$bandwidth"
        break
    else
        echo "Invalid selection. Please choose a valid number from the list."
    fi
done