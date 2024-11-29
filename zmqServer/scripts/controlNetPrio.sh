#!/bin/bash

LOCKFILE="/var/lock/controlNetPrio.lock"

# Function to add a tc filter and class
function add_tc_rule() {
    local interface="$1"
    local ip_addr="$2"
    local port="$3"
    local priority="$4"

    # Ensure priority is within a reasonable range (adjust as needed)
    if [[ $priority -lt 1 || $priority -gt 10 ]]; then
        echo "Invalid priority. Please enter a value between 1 and 10."
        return 1
    fi

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

    # Add class for priority if it doesn't already exist
    if ! tc class show dev "$interface" | grep -q "class htb 1:$priority"; then
        echo "Adding class 1:$priority for priority $priority."
        sudo tc class add dev "$interface" parent 1: classid 1:$priority htb rate 10mbit ceil 100mbit || {
            echo "Failed to add class 1:$priority for interface $interface."
            return 1
        }
    else
        echo "Class 1:$priority already exists for interface $interface."
    fi

    # Add filter for IP and port if it doesn't exist
    if ! tc filter show dev "$interface" | grep -q "match ip dst $ip_addr match ip dport $port"; then
        echo "Adding filter for IP $ip_addr, port $port, priority $priority."
        sudo tc filter add dev "$interface" protocol ip parent 1: prio "$priority" u32 \
            match ip dst "$ip_addr" match ip dport "$port" 0xffff flowid 1:$priority || {
            echo "Failed to add filter for IP $ip_addr, port $port, priority $priority."
            return 1
        }
    else
        echo "Filter for IP $ip_addr, port $port, priority $priority already exists."
    fi
}

# Ensure the script is called with proper arguments
if [[ $# -ne 4 ]]; then
    echo "Usage: $0 <interface> <ip_address> <port> <priority>"
    exit 1
fi

# Extract arguments
interface="$1"
ip_addr="$2"
port="$3"
priority="$4"

# Serialize access using a lock
(
    flock -x 200 || { echo "Failed to acquire lock. Another instance might be running."; exit 1; }

    echo "Applying TC rule: interface=$interface, IP=$ip_addr, port=$port, priority=$priority"
    add_tc_rule "$interface" "$ip_addr" "$port" "$priority"

) 200>"$LOCKFILE"
