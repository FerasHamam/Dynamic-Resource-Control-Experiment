import os
from datetime import datetime
import numpy as np
from ryu.base import app_manager
from ryu.controller import ofp_event
from ryu.controller.handler import MAIN_DISPATCHER, CONFIG_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.lib import hub
from ryu.ofproto import ofproto_v1_3
from ryu.app.simple_switch_13 import SimpleSwitch13

SLEEP_SEC = 4
MAX_HISTORY = 50  # Maximum number of historical data points to keep

# Fix syntax in PORT_EXCLUDED
PORT_AFFECTED = {'s1': ['s1-eth1']}
PORT_EXCLUDED = {'s1': ['s1-eth3', 's1-eth4']}

### For example, 100 Mbps:
SHARED_LINK_BW = 200  # in Mbps

### Number of consecutive deltas used for naive prediction
NUM_DELTAS = 5

class SimpleSwitchWithStats(SimpleSwitch13):
    def __init__(self, *args, **kwargs):
        super(SimpleSwitchWithStats, self).__init__(*args, **kwargs)
        self.datapaths = {}     # Store connected switches
        self.port_names = {}    # {dpid: {port_no: port_name}}

        # Historical data of deltas (not cumulative)
        self.stats_history = {}

        # Track the last cumulative reading to compute per-interval deltas
        self.last_cumulative = {}

        self.monitor_thread = hub.spawn(self._monitor)

    @set_ev_cls(ofp_event.EventOFPStateChange, [MAIN_DISPATCHER, CONFIG_DISPATCHER])
    def _state_change_handler(self, ev):
        datapath = ev.datapath
        if ev.state == MAIN_DISPATCHER:
            self.datapaths[datapath.id] = datapath
            self.logger.info(f"Switch {datapath.id} connected. Requesting port descriptions.")
            self._request_port_description(datapath)  # get port names
        elif ev.state == CONFIG_DISPATCHER:
            if datapath.id in self.datapaths:
                del self.datapaths[datapath.id]

    def _monitor(self):
        while True:
            for datapath in self.datapaths.values():
                self._request_port_stats(datapath)
            hub.sleep(SLEEP_SEC)

    def _request_port_stats(self, datapath):
        parser = datapath.ofproto_parser
        req = parser.OFPPortStatsRequest(datapath, 0, ofproto_v1_3.OFPP_ANY)
        datapath.send_msg(req)

    def _request_port_description(self, datapath):
        parser = datapath.ofproto_parser
        req = parser.OFPPortDescStatsRequest(datapath, 0)
        datapath.send_msg(req)

    @set_ev_cls(ofp_event.EventOFPPortDescStatsReply, MAIN_DISPATCHER)
    def _port_desc_reply_handler(self, ev):
        datapath = ev.msg.datapath
        dpid = datapath.id

        if dpid not in self.port_names:
            self.port_names[dpid] = {}

        for port in ev.msg.body:
            self.port_names[dpid][port.port_no] = port.name.decode('utf-8')

        self.logger.info("Port names for Switch %s: %s", dpid, self.port_names[dpid])

    @set_ev_cls(ofp_event.EventOFPPortStatsReply, MAIN_DISPATCHER)
    def _port_stats_reply_handler(self, ev):
        datapath = ev.msg.datapath
        body = ev.msg.body
        switch_id = datapath.id
        current_timestamp = datetime.now()

        self.logger.info("\n--- Port Stats for Switch %s (DELTA) ---", switch_id)

        for stat in body:
            port_no = stat.port_no
            key = (switch_id, port_no)

            # Current cumulative counters
            cum_rx_pkts = stat.rx_packets
            cum_tx_pkts = stat.tx_packets
            cum_rx_bytes = stat.rx_bytes
            cum_tx_bytes = stat.tx_bytes

            # Get port name
            port_name = self.port_names.get(switch_id, {}).get(port_no, f"Port-{port_no}")

            # Check if we have previous cumulative data
            if key in self.last_cumulative:
                prev_cum = self.last_cumulative[key]
                delta_rx_pkts  = cum_rx_pkts  - prev_cum["rx_pkts"]
                delta_tx_pkts  = cum_tx_pkts  - prev_cum["tx_pkts"]
                delta_rx_bytes = cum_rx_bytes - prev_cum["rx_bytes"]
                delta_tx_bytes = cum_tx_bytes - prev_cum["tx_bytes"]
            else:
                delta_rx_pkts  = 0
                delta_tx_pkts  = 0
                delta_rx_bytes = 0
                delta_tx_bytes = 0

            self.logger.info(
                "Port: %-10s | Δ RX Packets: %-10d | Δ TX Packets: %-10d | Δ RX Bytes: %-10d | Δ TX Bytes: %-10d",
                port_name, delta_rx_pkts, delta_tx_pkts, delta_rx_bytes, delta_tx_bytes
            )

            # Update last_cumulative to the current reading
            self.last_cumulative[key] = {
                "rx_pkts": cum_rx_pkts,
                "tx_pkts": cum_tx_pkts,
                "rx_bytes": cum_rx_bytes,
                "tx_bytes": cum_tx_bytes
            }

            # Store the delta in stats_history
            if key not in self.stats_history:
                self.stats_history[key] = []

            self.stats_history[key].append({
                "timestamp": current_timestamp,
                "rx_pkts": delta_rx_pkts,
                "tx_pkts": delta_tx_pkts,
                "rx_bytes": delta_rx_bytes,
                "tx_bytes": delta_tx_bytes
            })

            if len(self.stats_history[key]) > MAX_HISTORY:
                self.stats_history[key].pop(0)

        # Check if we need to analyze
        switch_name = f"s{switch_id}"
        if switch_name in PORT_AFFECTED:
            self.analyze_and_predict(switch_id, switch_name)

    # ---------------------------------------------------------
    #         Analysis + Prediction on the DELTA data
    # ---------------------------------------------------------
    def analyze_and_predict(self, switch_id, switch_name):
        """
        We analyze the Δ (delta) of RX bytes, predict the next interval's
        traffic in bits/second, and compare with SHARED_LINK_BW.
        """
        datapath = self.datapaths.get(switch_id)
        if not datapath:
            return

        predicted_affected_bps = 0.0
        predicted_others_bps = 0.0

        # We'll store predicted BPS per port for logging
        port_predictions_bps = {}

        for (dpid, port_no), data_list in self.stats_history.items():
            if dpid != switch_id:
                continue

            port_name = self.port_names[switch_id].get(port_no, port_no)
            # Skip analysis for excluded ports
            if port_name in PORT_EXCLUDED.get(switch_name, []):
                continue

            # Must have enough data for meaningful FFT
            if len(data_list) < NUM_DELTAS:
                continue

            # Extract the per-interval RX BYTES
            rx_bytes_deltas = [d["rx_bytes"] for d in data_list]

            # Apply FFT to the delta values
            fft_values = self.apply_fft(rx_bytes_deltas)

            # Naive prediction: average of last N deltas of BYTES
            predicted_bytes = int(np.mean(rx_bytes_deltas[-NUM_DELTAS:]))
            # Fix prediction later. 

            # Convert to bits per second.
            # We know each delta covers SLEEP_SEC intervals, so:
            predicted_bps = (predicted_bytes * 8.0) / SLEEP_SEC

            port_predictions_bps[port_name] = predicted_bps

            self.logger.info(
                "[Analysis] Switch: %s, Port: %s => Last %d Δ RX bytes: %s | FFT(partial): %s | Predicted next: %.2f bps",
                switch_id,
                port_name,
                NUM_DELTAS,
                rx_bytes_deltas[-NUM_DELTAS:],
                np.round(fft_values[1:6], 2),
                predicted_bps
            )

            # Check if port is 'affected' or 'other'
            if port_name in PORT_AFFECTED.get(switch_name, []):
                predicted_affected_bps += predicted_bps
            else:
                predicted_others_bps += predicted_bps

        sum_predicted_bps = predicted_affected_bps + predicted_others_bps
        self.logger.info(
            "[Prediction Summary] Affected= %.2f bps, Others= %.2f bps, Sum= %.2f bps",
            predicted_affected_bps, predicted_others_bps, sum_predicted_bps
        )

        # Compare with SHARED_LINK_BW in bits/second: (SHARED_LINK_BW * 1e6)
        threshold_bps = 0.9 * (SHARED_LINK_BW * 1e6)

        # TODO: fix conditions   
        #       if predicted_noise_bandwidth >= 0.5 * Shared Link:
        #           set our app to 50% of bandwidth 
        #       elif predicted_noise_bandwidth < 0.5 * Shared Link: 
        #             
        #           remove port qos // give full bandwidth to our port. 
        #
        
        if sum_predicted_bps < threshold_bps:
            self.logger.info(
                "Total predicted throughput < 90%% of link (%.2f bps). "
                "-> Start QoS function (protect Host1).",
                threshold_bps
            )
            # Suppose we interpret 'if there's congestion from host2, 
            # ensure host1 gets at least 50% of the link':
            for port_name in PORT_AFFECTED[switch_name]:
                self.apply_qos_rules(
                    datapath, 
                    port_name, 
                    0.5 * (SHARED_LINK_BW * 1e6)  # 50% of link in bps
                )
        else:
            # No special QoS or a different policy
            # TODO: remove all 
            self.logger.info(
                "Total predicted throughput >= 90%% of link (%.2f bps) -> No special QoS needed.",
                threshold_bps
            )

    def apply_fft(self, time_series):
        y = np.array(time_series, dtype=float)
        fft_result = np.fft.fft(y)
        return fft_result

    def apply_qos_rules(self, datapath, port_name, guaranteed_bps):
        """
        Placeholder for QoS rule logic.
        Here guaranteed_bps is in bits per second.
        """
        mbps = guaranteed_bps / 1e6
        self.logger.info(
            "Applying QoS rules to DP: %s, Port: %s, guaranteed BW: %.2f Mbps",
            datapath.id, port_name, mbps
        )
        # TODO: Implement actual QoS logic (e.g., queue configs, meters, etc.).
