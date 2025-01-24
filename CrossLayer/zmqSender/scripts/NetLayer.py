import json
import time
import subprocess
from pathlib import Path
import argparse

# Path to the JSON file monitored by the script
MONITOR_FILE = "congestion.json"

# IP and ports for tc configuration
# Parse command-line arguments
parser = argparse.ArgumentParser(description="Monitor and adjust tc settings based on JSON file.")
parser.add_argument("--dest_ip", type=str, required=True, help="Destination IP address")
parser.add_argument("--ports", type=int, nargs='+', required=True, help="List of ports to configure")
args = parser.parse_args()

DEST_IP = args.dest_ip
PORTS = args.ports
INTERFACE = "enp7s0"

# Get the directory of the current script
script_dir = Path(__file__).parent.resolve()
print ('DIR:', script_dir)

# Define the full paths to the shell scripts
add_classes_script = script_dir / "AddClasses.sh"
add_filters_script = script_dir / "AddFilters.sh"

def configure_tc(ports, bandwidths):
    """Configures traffic control for a specific port and bandwidth."""
    try:
        # Add classes
        subprocess.run(
            [
                "sudo", str(add_classes_script), f"{INTERFACE}", f"{bandwidths[0]}bit", f"{bandwidths[1]}bit"
            ],
            check=True
        )
        # add filters
        subprocess.run(
            [
                "sudo", str(add_filters_script), f"{INTERFACE}", f"{DEST_IP}", f"{ports[0]}", f"{ports[1]}"
            ],
            check=True
        )
        print(f"Configured tc for port {ports} with bandwidth {bandwidths} kbps")
    except subprocess.CalledProcessError as e:
        print(f"Error configuring tc for port {ports}: {e}")


def calculate_bandwidth(file_sizes, link_bandwidth, congestion):
    """Calculates the bandwidth for each port based on file sizes and congestion."""
    total_size = sum(file_sizes)
    available_bandwidth = link_bandwidth - congestion

    if available_bandwidth <= 0:
        print("No bandwidth available due to congestion.")
        return [0, 0]

    # Allocate bandwidth proportional to file sizes
    bandwidths = [int((size / total_size) * available_bandwidth) for size in file_sizes]
    bandwidths = [bw if bw > 0 else 100000 for bw in bandwidths]
    return bandwidths


def monitor_and_adjust():
    """Continuously monitors the file and adjusts tc settings."""
    last_values = {}

    while True:
        try:
            # Check if the file exists
            if not Path(MONITOR_FILE).is_file():
                print(f"File {MONITOR_FILE} not found. Waiting...")
                time.sleep(0.25)
                continue

            # Read the JSON file
            with open(MONITOR_FILE, "r") as f:
                data = json.load(f)

            file_sizes = data.get("file_sizes", [])
            link_bandwidth = data.get("link_bandwidth", 10000)  # Default 10 Mbps
            congestion = data.get("congestion", 0)              # Default no congestion

            if file_sizes != last_values:
                if last_values:
                    percentage_changes = [
                        abs((new - old) / old) * 100 if old != 0 else 0
                        for new, old in zip(file_sizes, last_values)
                    ]
                    if all(change <= 10 for change in percentage_changes):
                        print("Changes are within 10%. No need to adjust.")
                        time.sleep(0.15)
                        continue
                print("Detected changes in file values. Recalculating...")

                # Calculate bandwidths for ports
                bandwidths = calculate_bandwidth(file_sizes, link_bandwidth, congestion)

                # Adjust tc settings for each port
                configure_tc(PORTS, bandwidths)

                last_values = file_sizes

        except json.JSONDecodeError:
            print("Error decoding JSON. Ensure the file format is correct.")
        except Exception as e:
            print(f"An error occurred: {e}")

        # Wait for 250 ms before checking again
        time.sleep(0.15)

if __name__ == "__main__":
    monitor_and_adjust()