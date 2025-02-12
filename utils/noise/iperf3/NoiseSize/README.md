# 📡 Periodic iPerf3 Sender Script  

This script runs **iperf3** tests at a defined interval, logs the bandwidth results, and processes the output for analysis.  
Each test runs **at consistent intervals**, ensuring uniform gaps between iterations.

---

## 🚀 Features  

✔ Runs **iperf3** continuously at a specified interval.  
✔ Logs the bandwidth results in a structured format.  
✔ Converts **Kbits/sec** to **Mbits/sec** for consistency.  
✔ Supports custom **server IP, port, data size, and interval** settings.  
✔ Ensures **consistent time gaps** between each test iteration.  
✔ Logs results in a file named **`sender_port_<port>.txt`**.

---

## 🛠 Usage  

Run the script with the following arguments:  

```sh
./iperf_sender.sh <server_ip> <port> <size> <interval>
```

### 📌 Arguments  
- **`server_ip`** → The IP address of the iPerf3 server.  
- **`port`** → The port on which the iPerf3 server is listening.  
- **`size`** → The amount of data to send per test (e.g., `10M`).  
- **`interval`** → The interval (in seconds) between tests.  

### 📝 Example  
```sh
./iperf_sender.sh 192.168.1.100 5201 10M 5
```
This will run iPerf3 tests to **192.168.1.100:5201**, sending **10MB** of data every **5 seconds** with **consistent gaps** between iterations.

---

## 📂 Output Log Format  

The script logs data in the file **`sender_port_<port>.txt`**.  
Each entry follows this format:  

```
<duration>:<bitrate>
```

### 📜 Example Log Output  

```
1.00:0
0.99:85.50
0.99:92.30
0.99:88.20
0.99:90.75
0.99:87.60
0.99:89.40
```

### 🔍 Explanation  

- **First line (`1.00:0`)** → Initialization marker to sync logging.  
- **Subsequent lines** → Each test logs:
  - **Duration of transfer** (e.g., `0.99` seconds).  
  - **Bitrate in Mbits/sec** (e.g., `85.50 Mbps`).  
- **Each test occurs at precise, fixed intervals**, ensuring **consistent timing** between measurements.  

---

## 🔄 How It Works  

1️⃣ **Initial Setup**  
- The script verifies that all required arguments are provided.  
- It creates a log file named **`sender_port_<port>.txt`**.  

2️⃣ **Consistent Timing & Execution**  
- The script maintains **fixed gaps between tests** using the interval argument.  
- Each iteration waits exactly **`interval`** seconds before running again.  

3️⃣ **Data Processing & Logging**  
- Every **`interval`** seconds, an **iperf3** test runs.  
- The output is parsed to extract time intervals and bitrates.  
- Results are formatted and logged.  
- If the reported speed is in **Kbits/sec**, it's converted to **Mbits/sec**.  

---

## ❗ Notes  

- The script runs **indefinitely**
  #### - If running in the foreground, stop it with Ctrl+C
  #### - If running in the background, use the kill script
    ```sh
    ./iperf/kill_iperf3.sh
    ```

- Ensure **iperf3** is installed on both sender & receiver:  
  ```sh
  sudo apt install iperf3
  ```
- The **iperf3 server** should be running on the destination machine:  
  ```sh
  iperf3 -s -p <port>
  ```

---

## 🛠 Troubleshooting  

### ❌ "iperf3: command not found"  
Make sure **iperf3** is installed:  
```sh
sudo apt install iperf3
```

