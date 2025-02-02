import os
from ryu.base import app_manager
from ryu.controller import ofp_event
from ryu.controller.handler import MAIN_DISPATCHER, CONFIG_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.lib import hub
from ryu.ofproto import ofproto_v1_3
from ryu.app.simple_switch_13 import SimpleSwitch13  # Base class from Ryu

SLEEP_SEC = 4
BANDWIDTH_LIMIT = 400000000  # 200 Mbps in bits per second

class SimpleSwitchWithStats(SimpleSwitch13):
    def __init__(self, *args, **kwargs):
        super(SimpleSwitchWithStats, self).__init__(*args, **kwargs)
        self.datapaths = {}  # Store connected switches
        self.port_names = {}  # Maps {switch_id: {port_no: port_name}}
        self.monitor_thread = hub.spawn(self._monitor)  # Start background thread

    @set_ev_cls(ofp_event.EventOFPStateChange, [MAIN_DISPATCHER, CONFIG_DISPATCHER])
    def _state_change_handler(self, ev):
        """Handles switch connection and disconnection."""
        datapath = ev.datapath
        if ev.state == MAIN_DISPATCHER:
            self.datapaths[datapath.id] = datapath
            self.logger.info(f"Switch {datapath.id} connected. Requesting port descriptions and setting QoS.")
            self._request_port_description(datapath)  # Request port descriptions
            self.setup_qos(datapath.id)  # Apply QoS to all switch ports
        elif ev.state == CONFIG_DISPATCHER:
            if datapath.id in self.datapaths:
                del self.datapaths[datapath.id]

    def _monitor(self):
        """Periodically requests port statistics every SLEEP_SEC seconds."""
        while True:
            for datapath in self.datapaths.values():
                self._request_port_stats(datapath)
            hub.sleep(SLEEP_SEC)  # Wait before sending the next request

    def _request_port_stats(self, datapath):
        """Sends a port stats request to the switch."""
        parser = datapath.ofproto_parser
        req = parser.OFPPortStatsRequest(datapath, 0, ofproto_v1_3.OFPP_ANY)
        datapath.send_msg(req)

    def _request_port_description(self, datapath):
        """Sends a request to get port descriptions (names)."""
        parser = datapath.ofproto_parser
        req = parser.OFPPortDescStatsRequest(datapath, 0)
        datapath.send_msg(req)

    @set_ev_cls(ofp_event.EventOFPPortStatsReply, MAIN_DISPATCHER)
    def _port_stats_reply_handler(self, ev):
        """Handles port statistics reply from switch."""
        datapath = ev.msg.datapath
        body = ev.msg.body
        switch_id = datapath.id

        self.logger.info("\n--- Port Stats for Switch %s ---", switch_id)
        for stat in body:
            port_no = stat.port_no
            port_name = self.port_names.get(switch_id, {}).get(port_no, f"Port-{port_no}")

            self.logger.info(
                "Port: %-8s | RX Packets: %-10d | TX Packets: %-10d | RX Bytes: %-10d | TX Bytes: %-10d",
                port_name, stat.rx_packets, stat.tx_packets, stat.rx_bytes, stat.tx_bytes
            )

    @set_ev_cls(ofp_event.EventOFPPortDescStatsReply, MAIN_DISPATCHER)
    def _port_desc_reply_handler(self, ev):
        """Handles port description reply and stores port names."""
        datapath = ev.msg.datapath
        switch_id = datapath.id

        if switch_id not in self.port_names:
            self.port_names[switch_id] = {}

        for port in ev.msg.body:
            self.port_names[switch_id][port.port_no] = port.name.decode('utf-8')

        self.logger.info("Port names for Switch %s: %s", switch_id, self.port_names[switch_id])

    def setup_qos(self, switch_id):
        """
        Sets up QoS for all ports on the switch, limiting bandwidth to 200 Mbps.
        """
        self.logger.info(f"Applying QoS on switch {switch_id}...")

        # Get all ports on the switch using ovs-vsctl
        ports = os.popen(f"sudo ovs-vsctl list-ports s{switch_id}").read().splitlines()

        if not ports:
            self.logger.warning(f"No ports found for switch {switch_id}. QoS setup skipped.")
            return

        for port in ports:
            self.logger.info(f"Applying QoS to {port} on switch s{switch_id}")
            self.apply_qos_to_port(port)

    def apply_qos_to_port(self, port):
        """
        Apply QoS settings to a specific port to limit bandwidth to 200 Mbps.
        """
        os.system(f"""
            sudo ovs-vsctl -- set Port {port} qos=@qos \
                -- --id=@qos create QoS type=linux-htb other-config:max-rate={BANDWIDTH_LIMIT} \
                queues:1=@q1 \
                -- --id=@q1 create Queue other-config:min-rate={BANDWIDTH_LIMIT} other-config:max-rate={BANDWIDTH_LIMIT}
        """)

        self.logger.info(f"QoS applied to {port}: {BANDWIDTH_LIMIT / 1_000_000} Mbps")
