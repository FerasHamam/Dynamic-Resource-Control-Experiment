import time
import subprocess

def run_command(cmd):
    print(f"Executing: {cmd}")
    subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

def setup_tc():
    print("Setting up initial traffic control rules...")
    commands = [
        "sudo tc qdisc del dev enp9s0 root",
        "sudo tc qdisc add dev enp9s0 root handle 1: htb default 1",
        "sudo tc class add dev enp9s0 parent 1: classid 1:1 htb rate 400mbit ceil 400mbit",
        "sudo tc class add dev enp9s0 parent 1:1 classid 1:10 htb rate 200mbit ceil 400mbit",
        "sudo tc class add dev enp9s0 parent 1:1 classid 1:20 htb rate 200mbit ceil 200mbit",
        "sudo tc filter add dev enp9s0 protocol ip parent 1:0 u32 match ip dst 10.10.10.3 flowid 1:10",
        "sudo tc filter add dev enp9s0 protocol ip parent 1:0 u32 match ip dst 10.10.10.4 flowid 1:20",
        "sudo tc filter add dev enp9s0 protocol ip parent 1:0 u32 match ip dst 10.10.10.8 flowid 1:20"
    ]
    for cmd in commands:
        run_command(cmd)

def get_rx_bytes(interface):
    try:
        with open(f"/sys/class/net/{interface}/statistics/rx_bytes", "r") as f:
            return int(f.read().strip())
    except FileNotFoundError:
        print(f"Error: Interface {interface} not found.")
        return None

def update_tc_class_20(ceil_value, last_ceil):
    if ceil_value != last_ceil:
        print(f"Updating class 1:20 ceil to {ceil_value}mbit")
        cmd = f"sudo tc class change dev enp9s0 parent 1:1 classid 1:20 htb rate 200mbit ceil {ceil_value}mbit"
        run_command(cmd)
        return ceil_value
    return last_ceil

def monitor_interface(interface, threshold=5):
    zero_count = 0
    last_bytes = get_rx_bytes(interface)
    last_ceil = 200
    if last_bytes is None:
        return
    
    while True:
        time.sleep(1)
        current_bytes = get_rx_bytes(interface)
        if current_bytes is None:
            continue
        
        if current_bytes == last_bytes:
            zero_count += 1
        else:
            zero_count = 0  # Reset when data is received
        
        new_ceil = 400 if zero_count >= threshold else 200
        last_ceil = update_tc_class_20(new_ceil, last_ceil)
        
        print(f"RX bytes: {current_bytes}, Zero count: {zero_count}, Current ceil: {last_ceil}mbit")
        last_bytes = current_bytes

if __name__ == "__main__":
    setup_tc()
    monitor_interface("enp7s0")
