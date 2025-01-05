#!/bin/bash

list_interfaces() {
    echo "Available network interfaces:"
    interfaces=($(ls /sys/class/net))
    for i in "${!interfaces[@]}"; do
        echo "$i) ${interfaces[$i]}"
    done
}

apply_bandwidth_limit() {
    local interface="$1"
    local rate="$2"

    # Remove existing qdisc (if any)
    sudo tc qdisc del dev "$interface" root 2>/dev/null

    # Apply bandwidth limit using HTB qdisc
    sudo tc qdisc add dev "$interface" root handle 1: htb default 10
    sudo tc class add dev "$interface" parent 1: classid 1:1 htb rate "${rate}mbit"
    echo "Bandwidth limit of ${rate} Mbps applied to $interface."
}

list_interfaces
read -p "Select an interface by entering the number: " choice

interfaces=($(ls /sys/class/net))
if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 0 ] && [ "$choice" -lt "${#interfaces[@]}" ]; then
    selected_interface="${interfaces[$choice]}"
    echo "You selected: $selected_interface"
    apply_bandwidth_limit "$selected_interface" 200
else
    echo "Invalid choice. Exiting."
    exit 1
fi