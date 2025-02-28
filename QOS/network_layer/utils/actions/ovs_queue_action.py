import subprocess
from . import Action

# ----------------------------
# OvsQueueAction Implementation
# ----------------------------

class OvsQueueAction(Action):
    def __init__(self):
        # initial rate for queue2 in bits per second (bps)
        # (200000000 corresponds to 200mbit)
        self.last_queue2_rate = 200000000

    def run_command(self, cmd):
        print(f"Executing: {cmd}")
        subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def setup_qos(self, port):
        """
        Set up the initial Open vSwitch QoS configuration on the given port.
        This uses linux-htb with three queues:
            - queue 0: default (400mbit)
            - queue 1: priority 1 (200mbit min, 400mbit max)
            - queue 2: priority 2 (200mbit fixed)
        """
        print("Setting up initial OVS QoS configuration...")
        cmd = (
            f"sudo ovs-vsctl -- set port {port} qos=@newqos "
            f" -- --id=@newqos create qos type=linux-htb other-config:max-rate=400000000 "
            f" queues=0=@default,1=@queue1,2=@queue2 "
            f" -- --id=@default create queue other-config:min-rate=400000000 other-config:max-rate=400000000 "
            f" -- --id=@queue1 create queue other-config:min-rate=200000000 other-config:max-rate=400000000 other-config:priority=1 "
            f" -- --id=@queue2 create queue other-config:min-rate=200000000 other-config:max-rate=200000000 other-config:priority=2"
        )
        self.run_command(cmd)

    def update_qos_queue2(self, port, new_rate):
        """
        Update the configuration for queue2 on the given port.
        In this example, we assume updating both min-rate and max-rate.
        Only issues the command if the new rate differs from the last applied rate.
        """
        if new_rate != self.last_queue2_rate:
            print(f"Updating queue2 rates to {new_rate} bps on port {port}")
            cmd = (
                f"sudo ovs-vsctl set queue @queue2 "
                f"other-config:min-rate={new_rate} other-config:max-rate={new_rate}"
            )
            self.run_command(cmd)
            self.last_queue2_rate = new_rate

    # Implementation of the Action interface

    def install(self, port):
        """Install the QoS configuration on the given port."""
        self.setup_qos(port)

    def apply_on_port(self, port):
        """
        In this design, applying the action is done externally.
        This method just indicates that the action is applied on the port.
        """
        print(f"OvsQueueAction applied on port: {port}")

    def update_settings(self, **settings):
        """Update settings for OvsQueueAction if needed."""
        print("OvsQueueAction settings updated:", settings)
