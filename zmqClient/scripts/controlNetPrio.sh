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

    if [ ! -d "$group_path" ]; then
        echo "Creating cgroup: $group_name"
        mkdir "$group_path"
    else
        echo "Using existing cgroup: $group_name"
    fi
}

# Function to assign a process to a cgroup
assign_process() {
    local group_name=$1
    local pid=$2

    local group_path="/sys/fs/cgroup/net_prio/$group_name"
    echo "$pid" > "$group_path/cgroup.procs"
    echo "Assigned process $pid to cgroup $group_name."
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
