import socket
import time

def send_data(host, port, bandwidth_mbps, burst_duration, idle_duration):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    chunk_size = 64 * 1024  # 64 KB chunks
    data = b"x" * chunk_size  # Precompute data

    # Calculate desired bytes per second and sleep time per chunk
    bytes_per_second = (bandwidth_mbps * 10**6) / 8
    sleep_time = chunk_size / bytes_per_second

    try:
        while True:
            # Burst phase: Send data for `burst_duration` seconds
            burst_end = time.perf_counter() + burst_duration
            while time.perf_counter() < burst_end:
                send_start = time.perf_counter()
                sock.send(data)  # Send chunk
                elapsed = time.perf_counter() - send_start
                remaining_sleep = sleep_time - elapsed
                
                # Sleep only if needed and without overshooting burst time
                if remaining_sleep > 0:
                    max_sleep = burst_end - time.perf_counter()
                    actual_sleep = min(remaining_sleep, max_sleep)
                    time.sleep(actual_sleep)

            # Idle phase: Wait for `idle_duration` seconds
            time.sleep(idle_duration)

    except KeyboardInterrupt:
        print("Stopped sending.")
    finally:
        sock.close()

# Example: Send 20 Mbps bursts for 5 seconds, then idle for 5 seconds
send_data("10.0.0.4", 5001, bandwidth_mbps=20, burst_duration=5, idle_duration=5)