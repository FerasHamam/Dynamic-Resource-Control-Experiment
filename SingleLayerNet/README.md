# SimpleSwitchWithStats Controller with Static Meter Flows

This project implements a Ryu-based OpenFlow 1.3 controller that extends the default `simple_switch_13` behavior. In addition to the standard reactive MAC-learning flows, it installs **static meter flows** on specified ports to enforce QoS policies. For each affected port (as defined in the configuration), a meter is installed with a rate limit based on a shared link bandwidth. A static flow is added that matches on the input port, applies the meter, and then forwards packets normally using `OFPP_NORMAL`.

## Features

- **Reactive MAC Learning:**  
  Uses packet-in events to learn MAC addresses and install reactive flows for normal traffic forwarding.

- **Static Meter Flows:**  
  For ports listed in the `PORT_AFFECTED` constant (and not excluded via `PORT_EXCLUDED`), a meter is installed and a static flow is added that:
  - Matches on the input port (using the port number as a proxy for the port name, since OpenFlow does not match on port names).
  - Applies a meter instruction (with meter ID equal to the port number).
  - Forwards packets using the switch’s normal forwarding behavior (`OFPP_NORMAL`).

- **Statistics Collection and FFT Prediction:**  
  The controller collects port statistics every second and uses an FFT-based method for throughput prediction. Based on these predictions, QoS rules can be triggered (e.g., modifying the meter rate).

## Prerequisites

- **Python 3.6+**
- **Ryu Controller Framework** (Tested with Ryu 4.x and above)
- **Open vSwitch (OVS) 2.9+** (Running in OpenFlow 1.3 mode)
- **Mininet** (Optional, for testing in a virtual environment)

## Installation and Setup

### 1. Install Ryu

It is recommended to use a virtual environment:

# Create and activate a virtual environment (optional but recommended)
```bash
python3 -m venv ryu-env
source ryu-env/bin/activate

# Install Ryu via pip
pip install ryu
```

### 2. Install Open vSwitch

On Ubuntu, install Open vSwitch with:

```bash
sudo apt-get update
sudo apt-get install openvswitch-switch
```

Ensure that OVS is running and supports OpenFlow 1.3:

```bash
sudo ovs-vsctl set bridge <bridge_name> protocols=OpenFlow13
```

For example, if your bridge is named `br0`:

```bash
sudo ovs-vsctl set bridge br0 protocols=OpenFlow13
```

### 3. Set Up a Test Environment with Mininet (Optional)

Install Mininet:

```bash
sudo apt-get install mininet
```

Then launch a simple topology (for example):

```bash
sudo mn --controller=remote --topo=single,4 --mac
```

### 4. Configure OVS to Use the Controller

Assuming your controller will run on localhost and listen on port 6633, configure your OVS bridge:

```bash
sudo ovs-vsctl set-controller br0 tcp:127.0.0.1:6633
```

Verify the controller configuration:

```bash
sudo ovs-vsctl get-controller br0
```

### 5. Run the Controller

Save the provided code as, for example, `simple_switch_with_stats.py`.

Run the controller with:

```bash
ryu-manager simple_switch_with_stats.py
```

You should see logs indicating that switches are connecting, port descriptions are being received, meters are installed, and static meter flows are added.

## Code Overview

- **Constants & Configuration:**  
  - `SLEEP_SEC`, `WINDOW_SECONDS`, etc. define how often statistics are collected and how many samples are used.
  - `SHARED_LINK_BW` defines the shared bandwidth (in Mbps). **Note:** Meter rates are set in kilobits per second (kbps). For 200 Mbps, ensure you use the conversion factor `rate_kbps = int(SHARED_LINK_BW * 1000)` so that 200 Mbps equals 200,000 kbps.

- **Static Meter Flow Installation:**  
  The helper function `add_static_meter_flow(datapath, port_no)` installs a flow with:
  - A match on `in_port=port_no`
  - An instruction to apply a meter (meter ID = port number)
  - A normal forwarding action using `OFPP_NORMAL`

- **Reactive Flow Installation:**  
  The packet-in handler implements the standard MAC learning functionality and installs reactive flows based on source/destination MAC addresses.

- **QoS Prediction:**  
  The controller collects port statistics and uses an FFT-based prediction method. If the predicted throughput is below a threshold, QoS rules (e.g., modifying meter rates) may be applied.

## Troubleshooting

- **Devices Unreachable:**  
  If devices become unreachable, verify that both reactive (MAC-learning) flows and static meter flows are installed as expected. Use:

  ```bash
  sudo ovs-ofctl -O OpenFlow13 dump-flows <bridge_name>
  ```
  
  Ensure that the meter flows are not overriding essential reactive flows.

- **Meter Rate Issues:**  
  Verify that the conversion from Mbps to kbps is correct. For 200 Mbps, use:
  
  ```python
  rate_kbps = int(SHARED_LINK_BW * 1000)
  ```
  
  so that 200 Mbps equals 200,000 kbps.

## Acknowledgments

- [Ryu Controller Framework](https://ryu.readthedocs.io/)
- [Open vSwitch](http://www.openvswitch.org/)
- [Mininet](http://mininet.org/)
