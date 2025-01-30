#!/bin/bash

apply_bandwidth_limit() {
    local interface="$1"
    local bandwidth="$2"
    local direction="$3"
    local ifb_device="$4"                                                  # Use the dynamically assigned ifb device
    local classid="1:$(echo "$interface" | tr -d '[:alpha:]' | tr -d ':')" # Generate unique classid based on interface name

    # Remove any previous configurations
    sudo tc qdisc del dev "$interface" root 2>/dev/null

    if [[ "$direction" == "egress" ]]; then
        # Egress traffic limiting
        echo "Applying egress limit on $interface"
        sudo tc qdisc add dev "$interface" root handle 1: htb || return 1
        sudo tc class add dev "$interface" parent 1: classid "$classid" htb rate "$bandwidth" ceil "$bandwidth" || return 1
        sudo tc filter add dev "$interface" protocol ip parent 1:0 u32 match ip dst 0.0.0.0/0 flowid "$classid" || return 1
    elif [[ "$direction" == "ingress" ]]; then
        sudo tc qdisc del dev "$interface" ingress 2>/dev/null
        sudo tc qdisc del dev "$ifb_device" root 2>/dev/null
        # Ingress traffic limiting
        sudo tc qdisc del dev "$ifb_device" root 2>/dev/null

        # Load the ifb kernel module (required for ingress)
        sudo modprobe ifb || {
            echo "Failed to load ifb module"
            return 1
        }

        # Bring up the selected ifb device if it's not already up
        sudo ip link set "$ifb_device" up || {
            echo "Failed to bring up $ifb_device"
            return 1
        }

        echo "Applying ingress limit on $interface"
        # Redirecting flow to ifb Virtual Interface
        sudo tc qdisc add dev "$interface" handle ffff: ingress || return 1
        sudo tc filter add dev "$interface" parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev "$ifb_device" || return 1
        # Apply bandwidth limit on the ifb device
        sudo tc qdisc add dev "$ifb_device" root handle 1: htb || return 1
        sudo tc class add dev "$ifb_device" parent 1: classid "$classid" htb rate "$bandwidth" ceil "$bandwidth" || return 1
        sudo tc filter add dev "$ifb_device" protocol ip parent 1:0 u32 match ip src 0.0.0.0/0 flowid "$classid" || return 1
    else
        echo "Invalid direction specified: $direction"
        return 1
    fi
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
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <bandwidth> <direction>"
    echo "Example: $0 200mbit egress"
    echo "Example: $0 100mbit ingress"
    exit 1
fi

bandwidth="$1"
direction="$2"

interfaces=($(list_interfaces))

# Display a numbered list with interface name and MAC address for selection
echo "Available network interfaces (name-MAC address):"
select iface_info in "${interfaces[@]}"; do
    if [[ -n "$iface_info" ]]; then
        selected_interface=$(echo "$iface_info" | awk -F'-' '{print $1}')
        echo "You selected: $iface_info"

        # Dynamically create ifb device name based on the selected interface
        ifb_device="${selected_interface}_ifb"

        # Check if the ifb device exists, if not, create and bring it up
        if ! ip link show "$ifb_device" &>/dev/null; then
            echo "Creating $ifb_device for ingress shaping"
            sudo ip link add name "$ifb_device" type ifb
        fi

        echo "Using $ifb_device for ingress shaping"

        # Apply bandwidth limit to the selected interface and its corresponding ifb device
        apply_bandwidth_limit "$selected_interface" "$bandwidth" "$direction" "$ifb_device"
        break
    else
        echo "Invalid selection. Please choose a valid number from the list."
    fi
done
