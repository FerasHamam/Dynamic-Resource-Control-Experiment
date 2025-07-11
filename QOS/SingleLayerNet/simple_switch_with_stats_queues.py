import os 
import csv

from datetime import datetime
import numpy as np
from ryu.base import app_manager
from ryu.controller import ofp_event
from ryu.controller.handler import MAIN_DISPATCHER, CONFIG_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.lib import hub
from ryu.lib.packet import packet, ethernet
from ryu.ofproto import ofproto_v1_3
from ryu.app.simple_switch_13 import SimpleSwitch13

# =============================================================================
# Constants & Configuration
# =============================================================================
SLEEP_SEC = 6                      # Collect stats every 1 second
WINDOW_SECONDS = 18                 # 60 seconds of data
REQUIRED_HISTORY = WINDOW_SECONDS // SLEEP_SEC  # 60 samples needed (60/1)
MAX_HISTORY = REQUIRED_HISTORY      # Keep exactly one window (60 samples)
PREDICTION_SEGMENT_DURATION = 8   # Analyze a 10-second segment of prediction
PREDICTION_SEGMENT_SAMPLES = PREDICTION_SEGMENT_DURATION // SLEEP_SEC  # 10 samples (10/1)

SHARED_LINK_BW = 400                # Link capacity in Mbps

# Port configuration: only ports in PORT_AFFECTED will have meters installed.
PORT_AFFECTED = {'s1': ['s1-eth1']}
PORT_EXCLUDED = {'s1': ['s1-eth3', 's1-eth4']}  # not used now for meters
SHARED_LINK = "s1-eth3"

# =============================================================================
# Main Application Class
# =============================================================================
class SimpleSwitchWithStats(SimpleSwitch13):
    def __init__(self, *args, **kwargs):
        super(SimpleSwitchWithStats, self).__init__(*args, **kwargs)
        self.datapaths = {}         # Active switches: {dpid: datapath}
        self.port_names = {}        # {dpid: {port_no: port_name}}
        self.stats_history = {}     # {(dpid, port_no): [ {delta stats}, ... ]}
        self.last_cumulative = {}   # For computing per-interval deltas
        self.prediction_cache = {}  # Cache for FFT predictions per port.
        self.mac_to_port = {}       # MAC learning table
        self.csv_writers = {}       # CSV writer dictionary keyed by (dpid, port_no)
        # NEW: Track the last time history was cleared.
        self.last_history_reset = datetime.now()

        # Start threads:
        self.monitor_thread = hub.spawn(self._monitor)
        self.prediction_monitor_thread = hub.spawn(self._prediction_monitor)

    def _get_csv_writer(self, dpid, port_no, port_name):
        key = (dpid, port_no)
        if key not in self.csv_writers:
            # Create a filename – you can adjust the naming convention as needed.
            safe_port_name = port_name.replace(" ", "_")
            filename = f"port_{dpid}_{port_no}_{safe_port_name}.csv"
            csv_file = open(filename, 'a', newline='')
            writer = csv.writer(csv_file)
            # Write header row if file is newly created.
            writer.writerow(["timestamp", "rx_pkts", "tx_pkts", "rx_bytes", "tx_bytes"])
            self.csv_writers[key] = (csv_file, writer)
        return self.csv_writers[key][1]

    # -------------------------------------------------------------------------
    # Switch Event Handlers
    # -------------------------------------------------------------------------
    @set_ev_cls(ofp_event.EventOFPStateChange, [MAIN_DISPATCHER, CONFIG_DISPATCHER])
    def _state_change_handler(self, ev):
        datapath = ev.datapath
        if ev.state == MAIN_DISPATCHER:
            self.datapaths[datapath.id] = datapath
            self.logger.info("Switch %s connected. Requesting port descriptions.", datapath.id)
            self._request_port_description(datapath)
        elif ev.state == CONFIG_DISPATCHER:
            if datapath.id in self.datapaths:
                del self.datapaths[datapath.id]

    def _request_port_description(self, datapath):
        parser = datapath.ofproto_parser
        req = parser.OFPPortDescStatsRequest(datapath, 0)
        datapath.send_msg(req)

    # -------------------------------------------------------------------------
    # Data Collection (Every SLEEP_SEC seconds)
    # -------------------------------------------------------------------------
    def _monitor(self):
        """Request port stats from all switches every SLEEP_SEC seconds."""
        while True:
            for datapath in list(self.datapaths.values()):
                self._request_port_stats(datapath)
            hub.sleep(SLEEP_SEC)

    def _request_port_stats(self, datapath):
        parser = datapath.ofproto_parser
        req = parser.OFPPortStatsRequest(datapath, 0, ofproto_v1_3.OFPP_ANY)
        datapath.send_msg(req)

    @set_ev_cls(ofp_event.EventOFPPortDescStatsReply, MAIN_DISPATCHER)
    def _port_desc_reply_handler(self, ev):
        """
        Handle port description replies.
        """
        datapath = ev.msg.datapath
        dpid = datapath.id
        switch_name = f"s{dpid}"
        if dpid not in self.port_names:
            self.port_names[dpid] = {}
        for port in ev.msg.body:
            port_name = port.name.decode('utf-8')
            self.port_names[dpid][port.port_no] = port_name
        self.logger.info("Port names for Switch %s: %s", dpid, self.port_names[dpid])

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        """
        Handle packet in events.
        This is the default MAC learning behavior (from SimpleSwitch13).
        It learns the MAC addresses and installs reactive flows accordingly.
        """
        msg = ev.msg
        datapath = msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        dpid = datapath.id

        pkt = packet.Packet(msg.data)
        eth = pkt.get_protocols(ethernet.ethernet)[0]
        dst = eth.dst
        src = eth.src
        in_port = msg.match['in_port']

        self.mac_to_port.setdefault(dpid, {})
        self.mac_to_port[dpid][src] = in_port

        if dst in self.mac_to_port[dpid]:
            out_port = self.mac_to_port[dpid][dst]
        else:
            out_port = ofproto.OFPP_FLOOD

        actions = [parser.OFPActionOutput(out_port)]
        if out_port != ofproto.OFPP_FLOOD:
            match = parser.OFPMatch(in_port=in_port, eth_dst=dst, eth_src=src)
            # Install reactive flow with default priority.
            mod = parser.OFPFlowMod(datapath=datapath, priority=1,
                                    match=match,
                                    instructions=[parser.OFPInstructionActions(
                                        ofproto.OFPIT_APPLY_ACTIONS, actions)])
            datapath.send_msg(mod)
            self.logger.info("Reactive flow installed on DP %s: match=%s, actions=%s",
                             datapath.id, match, actions)

        data = None
        if msg.buffer_id == ofproto.OFP_NO_BUFFER:
            data = msg.data
        out = parser.OFPPacketOut(datapath=datapath,
                                  buffer_id=msg.buffer_id,
                                  in_port=in_port,
                                  actions=actions,
                                  data=data)
        datapath.send_msg(out)

    @set_ev_cls(ofp_event.EventOFPPortStatsReply, MAIN_DISPATCHER)
    def _port_stats_reply_handler(self, ev):
        """
        Every SLEEP_SEC seconds, compute per-interval deltas from cumulative counters
        and update stats_history for each port.
        """
        datapath = ev.msg.datapath
        body = ev.msg.body
        switch_id = datapath.id
        current_timestamp = datetime.now()

        self.logger.info("\n--- Port Stats for Switch %s (DELTA) ---", switch_id)
        for stat in body:
            port_no = stat.port_no
            key = (switch_id, port_no)
            cum_rx_pkts = stat.rx_packets
            cum_tx_pkts = stat.tx_packets
            cum_rx_bytes = stat.rx_bytes
            cum_tx_bytes = stat.tx_bytes

            port_name = self.port_names.get(switch_id, {}).get(port_no, f"Port-{port_no}")

            if key in self.last_cumulative:
                prev = self.last_cumulative[key]
                delta_rx_pkts = cum_rx_pkts - prev["rx_pkts"]
                delta_tx_pkts = cum_tx_pkts - prev["tx_pkts"]
                delta_rx_bytes = cum_rx_bytes - prev["rx_bytes"]
                delta_tx_bytes = cum_tx_bytes - prev["tx_bytes"]
            else:
                delta_rx_pkts = delta_tx_pkts = delta_rx_bytes = delta_tx_bytes = 0

            self.logger.info(
                "Port: %-10s | Δ RX Packets: %-10d | Δ TX Packets: %-10d | Δ RX Bytes: %-10d | Δ TX Bytes: %-10d",
                port_name, delta_rx_pkts, delta_tx_pkts, delta_rx_bytes, delta_tx_bytes
            )

            self.last_cumulative[key] = {
                "rx_pkts": cum_rx_pkts,
                "tx_pkts": cum_tx_pkts,
                "rx_bytes": cum_rx_bytes,
                "tx_bytes": cum_tx_bytes
            }

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

            # NEW: Log the port statistics to a CSV file for this port.
            writer = self._get_csv_writer(switch_id, port_no, port_name)
            writer.writerow([current_timestamp, delta_rx_pkts, delta_tx_pkts, delta_rx_bytes, delta_tx_bytes])
            # Optionally flush after each write to ensure the data is written to disk.
            self.csv_writers[key][0].flush()

    # -------------------------------------------------------------------------
    # FFT-Based Prediction & QoS Decision (Every PREDICTION_SEGMENT_DURATION seconds)
    # -------------------------------------------------------------------------
    def _prediction_monitor(self):
        """
        Every PREDICTION_SEGMENT_DURATION seconds, for each switch, process the prediction for each port,
        extract a segment from the stored full prediction, compute the average predicted throughput,
        and then decide if QoS rules should be applied for that switch.
        The history and prediction cache are cleared only after WINDOW_SECONDS seconds.
        """
        while True:
            hub.sleep(PREDICTION_SEGMENT_DURATION)
            current_time = datetime.now()
            # Dictionary to hold predictions grouped by switch (dpid)
            switch_predictions = {}

            for (dpid, port_no), history in self.stats_history.items():
                if len(history) < REQUIRED_HISTORY:
                    continue  # Not enough data collected yet

                port_name = self.port_names.get(dpid, {}).get(port_no, f"Port-{port_no}")
                switch_name = f"s{dpid}"
                if port_name in PORT_EXCLUDED.get(switch_name, []):
                    continue
                if switch_name not in PORT_AFFECTED.keys():
                    continue

                # Build the time series using the most recent REQUIRED_HISTORY samples.
                time_series = np.array([entry["rx_bytes"] for entry in history])
                port_key = (dpid, port_no)

                # Use the cached prediction if it's still valid; otherwise, compute a new one.
                if port_key in self.prediction_cache:
                    cached = self.prediction_cache[port_key]
                    if (current_time - cached['time']).total_seconds() < WINDOW_SECONDS:
                        full_prediction = cached['prediction']
                        prediction_time = cached['time']
                    else:
                        full_prediction = self.predict_future_with_ifft(time_series, REQUIRED_HISTORY)
                        self.prediction_cache[port_key] = {'prediction': full_prediction, 'time': current_time}
                        prediction_time = current_time
                else:
                    full_prediction = self.predict_future_with_ifft(time_series, REQUIRED_HISTORY)
                    self.prediction_cache[port_key] = {'prediction': full_prediction, 'time': current_time}
                    prediction_time = current_time

                elapsed = (current_time - prediction_time).total_seconds()
                sample_offset = int(elapsed / SLEEP_SEC)
                if sample_offset + PREDICTION_SEGMENT_SAMPLES <= len(full_prediction):
                    segment = full_prediction[sample_offset: sample_offset + PREDICTION_SEGMENT_SAMPLES]
                else:
                    segment = full_prediction[-PREDICTION_SEGMENT_SAMPLES:]
                predicted_bps_series = (segment * 8.0) / SLEEP_SEC
                avg_predicted_bps = np.mean(predicted_bps_series)

                self.logger.info(
                    "[Prediction Monitor] Switch: %s, Port: %s => Next %d sec mean: %.2f bps",
                    dpid, port_name, PREDICTION_SEGMENT_DURATION, avg_predicted_bps
                )

                if dpid not in switch_predictions:
                    switch_predictions[dpid] = {'affected': 0.0, 'others': 0.0, 'ports': []}
                switch_predictions[dpid]['ports'].append((port_name, avg_predicted_bps))
                if port_name in PORT_AFFECTED.get(switch_name, []):
                    switch_predictions[dpid]['affected'] += avg_predicted_bps
                else:
                    switch_predictions[dpid]['others'] += avg_predicted_bps

            # Process predictions for each switch.
            for dpid, pred in switch_predictions.items():
                total_predicted_bps = pred['affected'] + pred['others']
                self.logger.info(
                    "[Prediction Summary] Switch: %s => Affected= %.2f bps, Others= %.2f bps, Total= %.2f bps, Ports: %s",
                    dpid, pred['affected'], pred['others'], total_predicted_bps, pred['ports']
                )
                ###  
                # 
                # If our app needs bandwidth:
                #   1. If the predicted interfence is above a certain threshold, trigger QoS.
                #   2. If the predicted interfence is below a certain threshold, release QoS.
                # else:
                #   1. give the bandwidth to the other flows.
                # 
                # #
                threshold_bps_90 = 0.9 * (SHARED_LINK_BW * 1e6)
                threshold_bps_50 = 0.4 * (SHARED_LINK_BW * 1e6)
                if pred['affected'] > threshold_bps_50 :
                    if pred['others'] > threshold_bps_50:
                        self.logger.info(
                            "Switch: %s - Predicted throughput (%.2f bps) is above threshold (%.2f bps) -> Trigger QoS",
                            dpid, total_predicted_bps, threshold_bps_50
                        )
                        for (dpid_port, history) in self.stats_history.items():
                            dpid_curr, port_no = dpid_port
                            if dpid_curr != dpid:
                                continue
                            port_name = self.port_names.get(dpid, {}).get(port_no, f"Port-{port_no}")
                            if port_name not in PORT_EXCLUDED.get(f"s{dpid}", []):
                                if port_name not in PORT_AFFECTED.get(f"s{dpid}", []):
                                    self.logger.info("Q2 Switch: %s - Port: %s - Trigger QoS", dpid, port_name)
                                    self.apply_qos_queue(self.datapaths[dpid],port_name,2)
                                else:
                                    self.logger.info("Q1 Switch: %s - Port: %s - Trigger QoS", dpid, port_name)
                                    self.apply_qos_queue(self.datapaths[dpid],port_name,1)
                    else:
                        self.logger.info(
                        "Switch: %s - Predicted throughput (%.2f bps) is below threshold (%.2f bps) -> Trigger QoS",
                        dpid, total_predicted_bps, threshold_bps_90
                        )
                        for (dpid_port, history) in self.stats_history.items():
                            dpid_curr, port_no = dpid_port
                            if dpid_curr != dpid:
                                continue
                            port_name = self.port_names.get(dpid, {}).get(port_no, f"Port-{port_no}")
                            if port_name not in PORT_EXCLUDED.get(f"s{dpid}", []):
                                if port_name not in PORT_AFFECTED.get(f"s{dpid}", []):
                                    self.logger.info("Q1 Switch: %s - Port: %s - Trigger QoS", dpid, port_name)
                                    self.apply_qos_queue(self.datapaths[dpid],port_name,1)
                                else:
                                    self.logger.info("Q1 Switch: %s - Port: %s - Trigger QoS", dpid, port_name)
                                    self.apply_qos_queue(self.datapaths[dpid],port_name,1)
                        
                else:
                    self.logger.info(
                        "Switch: %s - Predicted throughput (%.2f bps) is below threshold (%.2f bps) -> Trigger QoS",
                        dpid, total_predicted_bps, threshold_bps_90
                    )
                    for (dpid_port, history) in self.stats_history.items():
                        dpid_curr, port_no = dpid_port
                        if dpid_curr != dpid:
                            continue
                        port_name = self.port_names.get(dpid, {}).get(port_no, f"Port-{port_no}")
                        if port_name not in PORT_EXCLUDED.get(f"s{dpid}", []):
                            if port_name not in PORT_AFFECTED.get(f"s{dpid}", []):
                                self.logger.info("Q1 Switch: %s - Port: %s - Trigger QoS", dpid, port_name)
                                self.apply_qos_queue(self.datapaths[dpid],port_name,1)
                            else:
                                self.logger.info("Q1 Switch: %s - Port: %s - Trigger QoS", dpid, port_name)
                                self.apply_qos_queue(self.datapaths[dpid],port_name,1)

            # NEW: Only clear the history and prediction cache after WINDOW_SECONDS (e.g. 1800 sec) have passed.
            if (current_time - self.last_history_reset).total_seconds() >= WINDOW_SECONDS+(REQUIRED_HISTORY*SLEEP_SEC):
                self.logger.info("Clearing stats history and prediction cache after %d seconds", WINDOW_SECONDS)
                self.stats_history = {}      # Clear all stats history
                self.prediction_cache = {}   # Clear prediction cache
                self.last_history_reset = current_time

    # -------------------------------------------------------------------------
    # FFT Prediction with Zero-Padding + IFFT
    # -------------------------------------------------------------------------
    def predict_future_with_ifft(self, time_series, n_predict):
        n = len(time_series)
        fft_coeffs = np.fft.fft(time_series)
        threshold = 0.25 * np.max(np.abs(fft_coeffs))
        fft_coeffs[np.abs(fft_coeffs) < threshold] = 0

        N = n + n_predict
        padded_fft = np.zeros(N, dtype=complex)
        half = (n // 2) + 1
        padded_fft[:half] = fft_coeffs[:half]
        padded_fft[-(n - half):] = fft_coeffs[half:]
        extended_signal = np.fft.ifft(padded_fft) * (N / n)
        return np.real(extended_signal[n:])

    def apply_qos_queue(self, datapath, port_name, queue_id):
        """
        Adjusts the queue assignment for a port by modifying the flow entry
        to include an action that sets the specified queue.

        :param datapath: The switch datapath object.
        :param port_name: Name of the port (as configured on the switch).
        :param queue_id: The preconfigured queue identifier to use.
        """
        dpid = datapath.id
        port_no = None
        # shared_link_port_no = None
        # Look up the port number using the port name.
        for p_no, p_name in self.port_names.get(dpid, {}).items():
            if p_name == port_name:
                port_no = p_no
                break
            # if p_name == SHARED_LINK:
            #     self.logger.info("Shared link port number: %s", p_no)
                # shared_link_port_no = p_no
        if port_no is None:
            self.logger.error("Port %s not found in DP %s", port_name, dpid)
            return

        parser = datapath.ofproto_parser
        ofproto = datapath.ofproto

        # Create a match for traffic entering on this port.
        # (You can adjust the match criteria as needed.)
        match = parser.OFPMatch(in_port=port_no )

        # Define the actions:
        # 1. Set the queue for the traffic using the preconfigured queue_id.
        # 2. Output the packet to the same port.
        actions = [
            parser.OFPActionSetQueue(queue_id),
            parser.OFPActionOutput(ofproto.OFPP_NORMAL)
        ]

        instructions = [
            parser.OFPInstructionActions(ofproto.OFPIT_APPLY_ACTIONS, actions)
        ]

        # Here we use OFPFC_MODIFY to change an existing flow.
        # Alternatively, you could remove and reinstall the flow.
        mod = parser.OFPFlowMod(
            datapath=datapath,
            command=ofproto.OFPFC_MODIFY,
            priority=20,
            match=match,
            instructions=instructions
        )

        datapath.send_msg(mod)
        self.logger.info("Modified queue on DP %s, Port %s (queue id %s)", dpid, port_name, queue_id)
