# Traffic Control (tc) Configuration Script  

This script configures **traffic control (tc)** using the **HTB (Hierarchical Token Bucket)** discipline on the `enp9s0` network interface **(which should be changed to your interface name)** to manage bandwidth allocation.

---

## 📌 What This Script Does  

### 1️⃣ Deletes Existing Traffic Control Rules  
- Removes any existing **filters**, **classes**, and **queuing disciplines (qdisc)** from `enp9s0`.

### 2️⃣ Sets Up a New Root Queuing Discipline  
- Adds an **HTB qdisc** with a **root handle `1:`** and a **default class `1:1`**.

### 3️⃣ Defines Bandwidth Classes  
| Class  | Parent | Guaranteed Rate | Max Rate |
|--------|--------|----------------|----------|
| `1:1`  | Root   | 400 Mbps       | 400 Mbps |
| `1:10` | `1:1`  | 200 Mbps       | 400 Mbps (burst) |
| `1:20` | `1:1`  | 200 Mbps       | 200 Mbps (limit) |

### 4️⃣ Applies Traffic Filters  
| Destination IP  | Assigned Class | Max Bandwidth |
|----------------|---------------|--------------|
| `10.10.10.3`  | `1:10`         | Up to 400 Mbps |
| `10.10.10.4`  | `1:20`         | Strict 200 Mbps |
| `10.10.10.8`  | `1:20`         | Strict 200 Mbps |

---

## 🎯 Effect  

✔ **10.10.10.3** is your **prioritized application receiver** and should be changed to your **own IP address**. It can dynamically utilize up to **400 Mbps**.  
✔ **10.10.10.4 and 10.10.10.8** are **other receiver applications** that you consider **as noise** and should be changed to your **own IP addresses**. They are **strictly limited to a shared 200 Mbps** to prevent interference.  
✔ The **root class (`1:1`)** ensures the total bandwidth does not exceed **400 Mbps**.  

This setup ensures that **10.10.10.3** gets **priority access to bandwidth**, while **other receivers** are throttled to prevent them from consuming too much network capacity.

---

## ⚡ Network Interface Explanation  

- `enp9s0` is the **output interface** where **all sender interfaces forward traffic** before it moves further into the network.  
- This script ensures that the **traffic shaping policies apply at this crucial exit point** to manage network congestion.

---

## ⚡ Usage  

Give execute permission:  
```sh
chmod +x traffic_control_script.sh
```
Run the script with **sudo**:  
```sh
sudo ./traffic_control_script.sh
```