import time
import threading
from collections import deque

class DataGatherer:
    def __init__(self, interface, threshold=5, max_seconds=1800):
        """
        :param interface: The network interface to monitor.
        :param threshold: (Unused in this version) The threshold for custom logic.
        :param max_seconds: Maximum number of data points to keep (1 point per second).
        """
        self.interface = interface
        self.threshold = threshold
        self.data = deque(maxlen=max_seconds)  # Automatically discards oldest data beyond max_seconds
        self.lock = threading.Lock()
        self.running = False
        self.thread = None

    def get_rx_bytes(self):
        try:
            with open(f"/sys/class/net/{self.interface}/statistics/rx_bytes", "r") as f:
                return int(f.read().strip())
        except FileNotFoundError:
            print(f"Error: Interface {self.interface} not found.")
            return None

    def _gather_data(self):
        """
        Background thread that collects rx_bytes data every second.
        """
        previous_bytes = self.get_rx_bytes()
        while self.running:
            time.sleep(1)
            current_bytes = self.get_rx_bytes()
            if current_bytes is not None and previous_bytes is not None:
                # Calculate bytes received during the last second
                delta = current_bytes - previous_bytes
                with self.lock:
                    self.data.append(delta)
                previous_bytes = current_bytes

    def start(self):
        """
        Start the data gathering thread.
        """
        if not self.running:
            self.running = True
            self.thread = threading.Thread(target=self._gather_data)
            self.thread.daemon = True  # Optional: daemonize thread so it stops with the main program
            self.thread.start()
            print("Data gathering started.")

    def stop(self):
        """
        Stop the data gathering thread.
        """
        if self.running:
            self.running = False
            if self.thread:
                self.thread.join()
            print("Data gathering stopped.")

    def get_data(self):
        """
        Returns a copy of the raw data gathered.
        """
        with self.lock:
            return list(self.data)

    def get_data_averaged(self, n):
        """
        Returns the data averaged over every n data points.
        For example, if data = [1,2,3,4,5,6] and n=2, it returns [1.5, 3.5, 5.5].
        """
        with self.lock:
            data_copy = list(self.data)
        averaged = []
        for i in range(0, len(data_copy), n):
            chunk = data_copy[i:i+n]
            if chunk:
                avg = sum(chunk) / len(chunk)
                averaged.append(avg)
        return averaged

# Example usage:
if __name__ == "__main__":
    # Create a DataGatherer for a specific interface, e.g., "enp7s0", with a maximum of 1800 seconds of data.
    gatherer = DataGatherer("enp7s0", max_seconds=1800)
    gatherer.start()

    # Let the thread run for a while (e.g., 10 seconds)
    time.sleep(10)

    # Stop gathering and print the collected data
    gatherer.stop()
    print("Raw data:", gatherer.get_data())
    print("Averaged data (every 2 points):", gatherer.get_data_averaged(2))
