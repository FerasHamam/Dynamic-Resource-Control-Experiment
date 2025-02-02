import socket
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import time
import threading

def receive_data(port, bandwidth_data):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("0.0.0.0", port))
    sock.listen(1)
    conn, addr = sock.accept()
    print(f"Connection from {addr}")

    start_time = time.time()
    total_data = 0

    while True:
        data = conn.recv(1024)
        if not data:
            break
        total_data += len(data)

        # Update bandwidth every second
        if time.time() - start_time >= 1:
            bandwidth_mbps = (total_data * 8) / (1024 * 1024)  # Convert to Mbps
            bandwidth_data.append(bandwidth_mbps)
            print(f"Received: {bandwidth_mbps:.2f} Mbps")
            total_data = 0
            start_time = time.time()

def plot_bandwidth(bandwidth_data):
    plt.style.use("ggplot")
    fig, ax = plt.subplots()
    ax.set_title("Received Bandwidth Over Time")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Bandwidth (Mbps)")

    def update(frame):
        ax.clear()
        ax.plot(bandwidth_data, label="Bandwidth (Mbps)")
        ax.legend(loc="upper left")
        ax.set_title("Received Bandwidth Over Time")
        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Bandwidth (Mbps)")

    ani = FuncAnimation(fig, update, interval=1000)
    plt.show()

# Shared list for bandwidth data
bandwidth_data = []
threading.Thread(target=receive_data, args=(5001, bandwidth_data)).start()
plot_bandwidth(bandwidth_data)
