import time
from typing import Dict, List
import numpy as np
from utils.data_gatherer import DataGatherer
from utils.predictors.fft_predictor import FftPredictor
from utils.actions.tc_queue_action import TCQueueAction

def run_experiment() -> None:
    """
    Gathers data from specified network ports for a set duration and uses file size
    information to dynamically adjust bandwidth allocation, favoring one port over others.
    Then applies TC actions based on the calculated bandwidth allocations.
    """
    # Network configuration
    ports: List[str] = ["s1-eth1", "s1-eth2"]  # Example port names; adjust as needed
    SWITCH_PORT = "s1-eth3"  # Port to apply TC actions to
    PRIORITIZED_PORT = "s1-eth1"    
    TOTAL_BANDWIDTH = 400  # Total bandwidth in Mbit
    MIN_BANDWIDTH = 50     # Minimum bandwidth for any port in Mbit
    GATHERDED_WINDOW = 1200
    FAVORING_FACTOR = 0.3
    IP_CONFIGS = {
        "enp7s0": ["10.10.10.4","10", "400", "400"],
        "enp8s0": ["10.10.10.5","20", "200", "400"],
        "enp9s0": ["10.10.10.6","30", "200", "400"],
        "enp10s0": ["10.10.10.10","30", "200", "400"],
    }

    # Initialize TC action handler
    action = TCQueueAction()
    commands = [ action.return_command(SWITCH_PORT,clssid,rate,ceil,ip) for ip,clssid,rate,ceil in IP_CONFIGS.values()]
    action.setup_tc_exp5(SWITCH_PORT,commands)
    
    # Initialize data gatherers
    gatherers: Dict[str, DataGatherer] = {port: DataGatherer(port, max_seconds=GATHERDED_WINDOW) for port in ports}

    # Start data gathering for all ports
    for gatherer in gatherers.values():
        gatherer.start()
        
    file_sizes: Dict[str, List[int]] = {port: [] for port in ports}
    zero_bandwidth_start: Dict[str, float] = {port: None for port in ports}
    transfer_data: Dict[str, Dict[str, any]] = {}
    start_time = time.time()
    
    # Initialize bandwidth allocations
    bandwidth_allocations: Dict[str, float] = {port: TOTAL_BANDWIDTH / len(ports) for port in ports}
    
    try:
        # Main experiment loop
        while time.time() - start_time < GATHERDED_WINDOW:
            for port, gatherer in gatherers.items():
                data = gatherer.get_data()
                bandwidth = data[-1] if data else 0
                
                # Track active transfers
                if port not in transfer_data:
                    transfer_data[port] = {"active": False, "start_time": None, "bytes_transferred": 0, "last_active": None}
                
                if bandwidth > 0:
                    # Transfer is active
                    if not transfer_data[port]["active"]:
                        # New transfer started
                        transfer_data[port]["active"] = True
                        transfer_data[port]["start_time"] = time.time()
                        transfer_data[port]["bytes_transferred"] = 0
                    
                    # Add current bandwidth to total bytes (convert Mbps to bytes per second)
                    transfer_data[port]["bytes_transferred"] += (bandwidth * 1000000 / 8)
                    transfer_data[port]["last_active"] = time.time()
                    zero_bandwidth_start[port] = None
                else:
                    if zero_bandwidth_start[port] is None:
                        zero_bandwidth_start[port] = time.time()
                    elif time.time() - zero_bandwidth_start[port] >= 3:
                        # Transfer complete, calculate file size
                        if transfer_data[port]["active"]:
                            # Calculate file size from accumulated bytes
                            file_size = int(transfer_data[port]["bytes_transferred"])
                            
                            # Add to history
                            file_sizes[port].append(file_size)
                            
                            # Reset transfer tracking
                            transfer_data[port]["active"] = False
                            
                            # Log transfer details
                            duration = transfer_data[port]["last_active"] - transfer_data[port]["start_time"]
                            print(f"Port {port}: Transfer complete. Size: {file_size/1000000:.2f} MB, Duration: {duration:.2f}s")
                        
                        zero_bandwidth_start[port] = None
            
            # Calculate bandwidth allocations based on file sizes if we have data
            if all(len(sizes) > 0 for sizes in file_sizes.values()):
                # Calculate average file size for each port
                avg_file_sizes = {port: np.mean(sizes) if sizes else 0 for port, sizes in file_sizes.items()}
                print("Current average file sizes:", avg_file_sizes)
                
                # Calculate bandwidth allocations using a weighted approach
                total_file_size = sum(avg_file_sizes.values())
                if total_file_size > 0:
                    # Base allocation based on file size proportion
                    base_allocations = {
                        port: (size / total_file_size) * (TOTAL_BANDWIDTH - MIN_BANDWIDTH * len(ports))
                        for port, size in avg_file_sizes.items()
                    }

                    # Add minimum bandwidth guarantee
                    bandwidth_allocations = {
                        port: base_allocations[port] + MIN_BANDWIDTH
                        for port in ports
                    }
                    
                    # Apply favoring factor to the selected port (increase by 30%)
                    if PRIORITIZED_PORT in bandwidth_allocations:
                        # Calculate how much extra bandwidth to give to the favored port
                        extra_bandwidth = bandwidth_allocations[PRIORITIZED_PORT] * FAVORING_FACTOR
                        
                        # Distribute the reduction among other ports proportionally
                        other_ports = [p for p in ports if p != PRIORITIZED_PORT]
                        if other_ports:
                            reduction_per_port = extra_bandwidth / len(other_ports)
                            for port in other_ports:
                                # Ensure we don't go below minimum bandwidth
                                max_reduction = max(0, bandwidth_allocations[port] - MIN_BANDWIDTH)
                                actual_reduction = min(reduction_per_port, max_reduction)
                                bandwidth_allocations[port] -= actual_reduction
                                extra_bandwidth -= actual_reduction
                            
                            # Add the accumulated extra bandwidth to the favored port
                            bandwidth_allocations[PRIORITIZED_PORT] += extra_bandwidth                
                for port, allocation in bandwidth_allocations.items():
                    print(f"Setting bandwidth for {port}: {allocation:.2f} Mbit")
                    action.update_tc_class_v2(SWITCH_PORT, int(allocation), IP_CONFIGS[port][1])

                    # Sleep briefly to allow TC changes to take effect
                    time.sleep(0.5)
            
            time.sleep(1)
            
        # Final report
        avg_file_sizes = {port: np.mean(sizes) if sizes else 0 for port, sizes in file_sizes.items()}
        print("\nExperiment Summary:")
        print("Average file sizes:", avg_file_sizes)
        print("Final bandwidth allocations:", bandwidth_allocations)
            
    except KeyboardInterrupt:
        print("Experiment interrupted by user.")
    finally:
        # Stop data gathering when done
        for gatherer in gatherers.values():
            gatherer.stop()
        print("Experiment completed.")

if __name__ == "__main__":
    run_experiment()