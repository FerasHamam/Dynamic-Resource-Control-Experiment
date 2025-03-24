import csv
import time
import datetime
import argparse
import signal
import sys
import os
from utils.data_gatherer import DataGatherer

class PortMonitor:
    def __init__(self, interfaces, output_dir="./data", interval=60):
        """
        Initialize the port monitor with multiple interfaces.
        
        :param interfaces: List of network interfaces to monitor
        :param output_dir: Directory to save CSV files
        :param interval: How often to save data to CSV (in seconds)
        """
        self.interfaces = interfaces
        self.output_dir = output_dir
        self.interval = interval
        self.gatherers : dict[str,DataGatherer] = {} # type: ignore
        self.running = False
        
        # Create output directory if it doesn't exist
        os.makedirs(output_dir, exist_ok=True)
        
    def start(self):
        """Start monitoring all specified interfaces"""
        self.running = True
        
        # Initialize a DataGatherer for each interface
        for interface in self.interfaces:
            self.gatherers[interface] = DataGatherer(interface)
            self.gatherers[interface].start()
        
        # Set up signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
        
        print(f"Started monitoring {len(self.interfaces)} interfaces: {', '.join(self.interfaces)}")
        print(f"Data will be saved every {self.interval} seconds to {self.output_dir}")
        
        # Start the saving loop
        self._save_loop()
            
    def stop(self):
        """Stop all data gatherers and save final data"""
        if not self.running:
            return
            
        self.running = False
        print("\nStopping port monitoring...")
        
        # Stop all gatherers
        for interface, gatherer in self.gatherers.items():
            gatherer.stop()
            
        # Save final data
        self._save_data()
        
        print("Port monitoring stopped. All data saved.")
        
    def signal_handler(self, sig, frame):
        """Handle termination signals"""
        self.stop()
        sys.exit(0)
        
    def _save_loop(self):
        """Periodically save data to CSV files"""
        last_save_time = time.time()
        
        try:
            while self.running:
                current_time = time.time()
                
                # Check if it's time to save data
                if current_time - last_save_time >= self.interval:
                    self._save_data()
                    last_save_time = current_time
                    
                time.sleep(1)
        except Exception as e:
            print(f"Error in save loop: {e}")
            self.stop()
            
    def _save_data(self):
        """Save the current data for all interfaces to a single CSV file with columns for each interface"""
        # Collect data from all interfaces first
        all_data = {}
        timestamp_to_data = {}
        
        for interface, gatherer in self.gatherers.items():
            # Get the collected data
            data = gatherer.get_data()
            
            if not data:
                print(f"No data to save for interface {interface}")
                continue
                
            all_data[interface] = data
            
            # Calculate the timestamps for each data point (assuming 1 point per second)
            end_time = time.time()
            timestamps = [end_time - (len(data) - i) for i in range(len(data))]
            
            # Map timestamps to data values for this interface
            for i, rx_bytes in enumerate(data):
                dt = datetime.datetime.fromtimestamp(timestamps[i])
                dt_str = dt.strftime("%Y-%m-%d %H:%M:%S")
                
                if dt_str not in timestamp_to_data:
                    timestamp_to_data[dt_str] = {}
                
                timestamp_to_data[dt_str][interface] = rx_bytes
        
        if not timestamp_to_data:
            print("No data to save for any interface")
            return
        
        # Create the filename
        filename = f"{self.output_dir}/data.csv"
        
        # Check if file exists to determine if we need to write headers
        file_exists = os.path.isfile(filename)
        
        # Create a sorted list of all interfaces for consistent column ordering
        all_interfaces = sorted(all_data.keys())
        
        # Open file in append or write mode
        mode = 'a' if file_exists else 'w'
        with open(filename, mode, newline='') as csvfile:
            csv_writer = csv.writer(csvfile)
            
            # Write header row only if file is new
            if not file_exists:
                header_row = ['timestamp'] + all_interfaces
                csv_writer.writerow(header_row)
            
            # Sort timestamps for chronological order
            sorted_timestamps = sorted(timestamp_to_data.keys())
            
            # Write data rows
            for timestamp in sorted_timestamps:
                row = [timestamp]
                
                # Add data for each interface, or blank if not available
                for interface in all_interfaces:
                    if interface in timestamp_to_data[timestamp]:
                        row.append(timestamp_to_data[timestamp][interface])
                    else:
                        row.append('')
                
                csv_writer.writerow(row)
        
        print(f"Saved data for all interfaces to {filename}")
            
def main():
    parser = argparse.ArgumentParser(description='Monitor rx_bytes for network interfaces and save to CSV')
    parser.add_argument('interfaces', nargs='+', help='List of network interfaces to monitor')
    parser.add_argument('--output-dir', default='./data', help='Directory to save CSV files')
    parser.add_argument('--interval', type=int, default=60, help='How often to save data (in seconds)')
    
    args = parser.parse_args()
    
    monitor = PortMonitor(
        interfaces=args.interfaces,
        output_dir=args.output_dir,
        interval=args.interval
    )
    
    monitor.start()
    
if __name__ == "__main__":
    main()