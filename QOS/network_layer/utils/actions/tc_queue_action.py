import subprocess
from . import Action

# ----------------------------
# QueueAction Implementation
# ----------------------------

class TCQueueAction(Action):
    def __init__(self):
        # initial ceiling in mbit for tc class 1:20
        self.last_ceil = 200

    def run_command(self, cmd):
        print(f"Executing: {cmd}")
        subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def return_command(self, interface,clssid,rate,ceil,ip):
        """Return the command to set up the tc class."""
        
        commands = [
            f"sudo tc class add dev {interface} parent 1:1 classid 1:{clssid} htb rate {rate}mbit ceil {ceil}mbit",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst {ip} flowid 1:{clssid}"
        ]
        return commands
        
    def setup_tc_exp5(self, interface, setup_commands):
        """Set up the initial traffic control (tc) rules on the given interface for experiment 5."""
        print("Setting up initial traffic control rules for experiment 5...")
        commands = [
            f"sudo tc qdisc del dev {interface} root",
            f'sudo tc qdisc add dev {interface} root handle 1: htb default 40',
            f"sudo tc qdisc add dev {interface} parent 1: classid 1:1 htb rate 400mbit ceil 400mbit",
            setup_commands
        ]
        
        for cmd in commands:
            if isinstance(cmd, list):
                for sub_cmd in cmd:
                    self.run_command(sub_cmd)
            else:
                self.run_command(cmd)


    def setup_tc(self, interface):
        """Set up the initial traffic control (tc) rules on the given interface."""
        print("Setting up initial traffic control rules...")
        commands = [
            f"sudo tc qdisc del dev {interface} root",
            f"sudo tc qdisc add dev {interface} root handle 1: htb default 1",
            f"sudo tc class add dev {interface} parent 1: classid 1:1 htb rate 400mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:10 htb rate 200mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:20 htb rate 200mbit ceil 400mbit",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.4 flowid 1:10",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.5 flowid 1:20",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.6 flowid 1:20",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.10 flowid 1:20"
        ]
        for cmd in commands:
            self.run_command(cmd)
            
    def setup_tc_exp5(self, interface):
        """Set up the initial traffic control (tc) rules on the given interface."""
        print("Setting up initial traffic control rules...")
        commands = [
            f"sudo tc qdisc del dev {interface} root",
            f"sudo tc qdisc add dev {interface} root handle 1: htb default 1",
            f"sudo tc class add dev {interface} parent 1: classid 1:1 htb rate 400mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:10 htb rate 200mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:20 htb rate 200mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:30 htb rate 200mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:40 htb rate 200mbit ceil 400mbit",
            f"sudo tc class add dev {interface} parent 1:1 classid 1:50 htb rate 200mbit ceil 400mbit",
            
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.4 flowid 1:10",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.5 flowid 1:20",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.6 flowid 1:30",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.10 flowid 1:40",
            f"sudo tc filter add dev {interface} protocol ip parent 1:0 u32 match ip dst 10.10.10.10 flowid 1:50"
            
        ]
        for cmd in commands:
            self.run_command(cmd)

    def update_tc_class_20(self, interface, ceil_value):
        """Update the tc class 1:20 ceiling if its value changes."""
        if ceil_value != self.last_ceil:
            print(f"Updating class 1:20 ceil to {ceil_value}mbit")
            cmd = f"sudo tc class change dev {interface} parent 1:1 classid 1:20 htb rate 200mbit ceil {ceil_value}mbit"
            self.run_command(cmd)
            self.last_ceil = ceil_value
    
    def update_tc_class_v2(self, interface, value,cls):
        """Update the tc class 1:{cls} ceiling if its value changes."""
        print(f"Updating class 1:{cls} ceil to {value}mbit")
        cmd = f"sudo tc class change dev {interface} parent 1:1 classid 1:{cls} htb rate {value}mbit ceil {value}mbit"
        self.run_command(cmd)
        
    def update_tc_class_v3(self, interface, min_value, max_value, cls):
        """Update the tc class 1:{cls} ceiling if its value changes."""
        print(f"Updating class 1:{cls} rate to {min_value}mbit")
        cmd = f"sudo tc class change dev {interface} parent 1:1 classid 1:{cls} htb rate {min_value}mbit ceil {max_value}mbit"
        self.run_command(cmd)
    

    # Implementation of the Action interface
    def install(self, port):
        """Set up the tc rules on the given port (network interface)."""
        self.setup_tc(port)

    def apply_on_port(self, port):
        """In this design, applying the action is done externally via data gathered in the experiment."""
        print(f"QueueAction applied on port: {port}")

    def update_settings(self, **settings):
        """Update settings for QueueAction if needed."""
        print("QueueAction settings updated:", settings)