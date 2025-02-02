# Periodic Network Bandwidth Simulation

This project demonstrates a network simulation setup using `iperf3` for monitoring bandwidth performance between a sender and a receiver. The goal is to simulate traffic over specific ports with defined bandwidth limits.

## Requirements
- `iperf3` installed on both sender and receiver machines
- `bash` shell for running the scripts

---

## Setup

### 1. Install `iperf3`
To install `iperf3`, run the following command:

```bash
sudo apt install iperf3
```

---

## Usage

### Run Receiver Example
The following command sets up a receiver that listens on port `4444` and logs the results with timestamps to `receiver_port_4444.txt`.

```bash
sudo iperf3 -s -p 4444 --timestamps > receiver_port_4444.txt
```

- **`-s`**: Run `iperf3` as a server.
- **`-p 4444`**: Specify port `4444`.
- **`--timestamps`**: Add timestamps to the output.

---

### Run Sender
The sender is configured using the provided shell script, `run_periodic_sender.sh`. This example command sends periodic traffic to the receiver at port `4444` with a bandwidth of `50M` (50 Mbps) every 10 seconds.

```bash
./run_periodic_sender.sh "0.0.0.0" 4444 15 "50M" 10
```

#### Explanation of Parameters:
- **`"0.0.0.0"`**: The receiver's IP address. Replace it with the correct IP if needed.
- **`4444`**: The port to send traffic to.
- **`15`**: Interval in seconds between sending data.
- **`"50M"`**: The bandwidth limit per test (50 Mbps).
- **`10`**: Duration in seconds for each test.

---

## Example Output
Receiver logs will be saved to `receiver_port_4444.txt` and include metrics like:
- Bandwidth usage
- Throughput
- Timestamped logs for analysis

---

## Customization
- Modify the **port** number in the `iperf3` commands to simulate different scenarios.
- Adjust the **bandwidth** parameter (`50M`) to test various load conditions.
