#!/bin/bash

#This bash script works for cgroup v2
#Run this mount | grep cgroup
#output shoulde be: cgroup2 on /sys/fs/cgroup type cgroup2 (rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot)


# Check if the net_prio cgroup is mounted
if ! mount | grep -q "net_prio"; then
	echo "Mounting net_prio cgroup!"
	sudo mount -t cgroup -o net_prio none /sys/fs/cgroup/net_prio
fi

# Function to create or use an existing cgroup
create_cgroup() {
    local group_name=$1
    local group_path="/sys/fs/cgroup/net_prio/$group_name"
    local lock_file="$group_path.lock"

    # Acquire the lock
    while [ -f "$lock_file" ]; do
        sleep 0.1
    done
    touch "$lock_file"

    if [ ! -d "$group_path" ]; then
        echo "Creating cgroup: $group_name"
        mkdir "$group_path"
    else
        echo "Using existing cgroup: $group_name"
    fi

    # Release the lock
    rm "$lock_file"
}

# Function to assign a process to a cgroup
assign_process() {
    local group_name=$1
    local pid=$2

    local group_path="/sys/fs/cgroup/net_prio/$group_name"
    echo "$pid" > "$group_path/cgroup.procs"
    echo "Assigned process $pid to cgroup $group_name."
}

set_netprio() {
    local group_name=$1
    local interface=$2
    local priority=$3

    # The path to the net_prio cgroup for the given group name
    local group_path="/sys/fs/cgroup/net_prio/$group_name"

    # Ensure the cgroup exists, create it if necessary
    if [ ! -d "$group_path" ]; then
        echo "Creating cgroup: $group_name"
        mkdir "$group_path"
    else
        echo "Using existing cgroup: $group_name"
    fi

    # Set the network interface priority for the group
    echo "$priority" > "$group_path/net_prio.$interface"

    echo "Set network priority for $group_name on $interface to $priority"
}

# Start
# Check input arguments
if [ "$#" -lt 4 ]; then
    echo "Usage: $0 <group_name> <PID> <interface> <priority>"
    echo "Example: $0 high_prio 12345 eth0 10"
    exit 1
fi

# Parse arguments
GROUP_NAME=$1
PID=$2
INTERFACE=$3
PRIORITY=$4

# Create or use the cgroup
GROUP_PATH=$(create_cgroup "$GROUP_NAME")

# Set network priority
set_netprio "$GROUP_NAME" "$INTERFACE" "$PRIORITY"

# Assign the process to the cgroup
assign_process "$GROUP_NAME" "$PID"

echo "Network priority dynamically controlled for process $PID."
