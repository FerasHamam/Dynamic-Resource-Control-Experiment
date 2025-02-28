
import subprocess
from . import Action

class MeterAction(Action):
    def __init__(self, meter_id=None, rate_kbps=400000, burst_size=None):
        """
        :param meter_id: Optional meter id. If not provided, defaults to the port number.
        :param rate_kbps: Meter rate in kbps.
        :param burst_size: Meter burst size in kbps (default: 10% of rate_kbps).
        """
        self.meter_id = meter_id
        self.rate_kbps = rate_kbps
        self.burst_size = burst_size if burst_size is not None else int(rate_kbps * 0.1)

    def install(self, port):
        """
        Installs a meter entry on the given port using ovs-ofctl.
        :param port: A dictionary containing:
                     - 'bridge': name of the OVS bridge.
                     - 'port_no': port number.
                     Optionally, 'port_name'.
        """
        bridge = port['bridge']
        # Use the port number as meter_id if not explicitly set.
        if self.meter_id is None:
            self.meter_id = port['port_no']
        
        # Construct the ovs-ofctl command to add a meter.
        # Example: ovs-ofctl -O OpenFlow13 add-meter br0 meter=1,flags=kbps,band=drop,rate=400000,burst=40000
        cmd = [
            "ovs-ofctl", "-O", "OpenFlow13", "add-meter", bridge,
            f"meter={self.meter_id},flags=kbps,band=drop,rate={self.rate_kbps},burst={self.burst_size}"
        ]
        subprocess.run(cmd, check=True)
        print(f"[MeterAction] Installed meter on bridge {bridge}, port {port.get('port_name', port['port_no'])} "
              f"(meter id {self.meter_id}, rate {self.rate_kbps} kbps, burst {self.burst_size}).")

    def apply_on_port(self, port):
        """
        Installs a static flow on the specified port that applies the meter,
        using ovs-ofctl.
        :param port: A dictionary containing:
                     - 'bridge': name of the OVS bridge.
                     - 'port_no': port number.
        """
        bridge = port['bridge']
        port_no = port['port_no']
        # Build the flow command.
        # Example: ovs-ofctl -O OpenFlow13 add-flow br0 "in_port=1,actions=meter:1,normal"
        flow = f"in_port={port_no},actions=meter:{self.meter_id},normal"
        cmd = ["ovs-ofctl", "-O", "OpenFlow13", "add-flow", bridge, flow]
        subprocess.run(cmd, check=True)
        print(f"[MeterAction] Applied static meter flow on bridge {bridge}, port {port_no} using meter id {self.meter_id}.")

    def update_settings(self, **settings):
        """
        Update meter configuration parameters.
        Note: This updates internal settings. To apply changes on the switch,
        you will need to reinstall or modify the meter using the appropriate OVS command.
        """
        if 'rate_kbps' in settings:
            self.rate_kbps = settings['rate_kbps']
        if 'burst_size' in settings:
            self.burst_size = settings['burst_size']
        print(f"[MeterAction] Updated settings: rate {self.rate_kbps} kbps, burst {self.burst_size}.")