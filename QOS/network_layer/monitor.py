import time
import os
import csv
from datetime import datetime

def get_rx_bytes(interface):
    """Read the rx_bytes value for a given network interface."""
    try:
        with open(f"/sys/class/net/{interface}/statistics/rx_bytes", "r") as f:
            return int(f.read().strip())
    except (FileNotFoundError, IOError):
        return None

def monitor_interfaces(interfaces, interval=1, csv_filename=None):
    """Monitor rx_bytes for multiple interfaces and print/log updates."""
    # Store previous values to calculate rate
    prev_values = {}
    
    # Get initial values
    for interface in interfaces:
        rx_bytes = get_rx_bytes(interface)
        if rx_bytes is not None:
            prev_values[interface] = rx_bytes
        else:
            print(f"Interface {interface} not found or cannot be accessed")
    
    # Set up CSV file if filename is provided
    csv_file = None
    csv_writer = None
    
    if csv_filename:
        # Create file and write header
        csv_file = open(csv_filename, 'w', newline='')
        csv_writer = csv.writer(csv_file)
        
        # Write header row
        header = ['Timestamp']
        for interface in interfaces:
            if interface in prev_values:
                header.extend([f"{interface}_mbps"])
        csv_writer.writerow(header)
    
    print(f"{'Interface':<10} {'Rate (Mbps)':<15}")
    print("-" * 40)
    
    try:
        while True:
            # Precise 1-second interval
            start_time = time.time()
            
            # Get current timestamp
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            # Prepare row for CSV
            if csv_writer:
                csv_row = [timestamp]
            
            for interface in interfaces:
                if interface not in prev_values:
                    continue
                    
                rx_bytes = get_rx_bytes(interface)
                if rx_bytes is None:
                    print(f"Lost access to {interface}")
                    del prev_values[interface]
                    continue
                
                # Calculate rate in bytes per second
                bytes_per_second = rx_bytes - prev_values[interface]
                prev_values[interface] = rx_bytes
                
                # Convert to Mbps (megabits per second)
                # 1 byte = 8 bits, and 1 Mbps = 1,000,000 bits per second
                mbps = (bytes_per_second * 8) / 1_000_000
                
                # Print current values
                print(f"{interface:<10} {mbps:.2f} Mbps")
                
                # Add to CSV row if logging
                if csv_writer:
                    csv_row.extend([mbps])
            
            # Write the row to CSV
            if csv_writer:
                csv_writer.writerow(csv_row)
                # Flush to ensure data is written immediately
                csv_file.flush()
            
            print("-" * 40)  # Separator between updates
            
            # Calculate time elapsed and sleep precisely for 1 second
            elapsed = time.time() - start_time
            sleep_time = max(0, interval - elapsed)
            time.sleep(sleep_time)
            
    except KeyboardInterrupt:
        print("\nMonitoring stopped.")
        if csv_file:
            csv_file.close()
            print(f"Data saved to {csv_filename}")

if __name__ == "__main__":
    # List of interfaces to monitor
    interfaces = ["enp8s0", "enp7s0", "enp10s0", "enp11s0"]
    
    # Create a CSV filename with timestamp
    csv_filename = "data.csv"
    
    print(f"Starting to monitor {len(interfaces)} interfaces...")
    print(f"Data will be saved to {csv_filename}")
    
    # Start monitoring with exactly 1 second interval
    monitor_interfaces(interfaces, interval=1, csv_filename=csv_filename)