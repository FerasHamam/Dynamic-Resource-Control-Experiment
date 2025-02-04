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

# =============================================================================
# Constants & Configuration
# =============================================================================
# For quick testing:
SLEEP_SEC = 1                       # Collect stats every 1 second
WINDOW_SECONDS = 60                 # 60 seconds of data
REQUIRED_HISTORY = WINDOW_SECONDS // SLEEP_SEC  # 60 samples needed (60/1)
MAX_HISTORY = REQUIRED_HISTORY      # Keep exactly one window (60 samples)
PREDICTION_SEGMENT_DURATION = 10    # Analyze a 10-second segment of prediction
PREDICTION_SEGMENT_SAMPLES = PREDICTION_SEGMENT_DURATION // SLEEP_SEC  # 10 samples (10/1)

SHARED_LINK_BW = 200                # Link capacity in Mbps

# Port configuration
PORT_AFFECTED = {'s1': ['s1-eth1']}
PORT_EXCLUDED = {'s1': ['s1-eth3', 's1-eth4']}

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
        # Cache for full FFT predictions per port.
        # Key: (dpid, port_no) -> Value: {'prediction': np.array, 'time': datetime}
        self.prediction_cache = {}

        # Start threads:
        self.monitor_thread = hub.spawn(self._monitor)
        # IMPORTANT: Use the prediction segment duration (10 sec) here,
        # so the prediction monitor runs every 10 seconds.
        self.prediction_monitor_thread = hub.spawn(self._prediction_monitor)

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
        datapath = ev.msg.datapath
        dpid = datapath.id
        if dpid not in self.port_names:
            self.port_names[dpid] = {}
        for port in ev.msg.body:
            # Decode port name if necessary.
            self.port_names[dpid][port.port_no] = port.name.decode('utf-8')
        self.logger.info("Port names for Switch %s: %s", dpid, self.port_names[dpid])

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
            # Cumulative counters
            cum_rx_pkts = stat.rx_packets
            cum_tx_pkts = stat.tx_packets
            cum_rx_bytes = stat.rx_bytes
            cum_tx_bytes = stat.tx_bytes

            port_name = self.port_names.get(switch_id, {}).get(port_no, f"Port-{port_no}")

            # Compute per-interval delta if previous reading exists
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

            # Update last cumulative values
            self.last_cumulative[key] = {
                "rx_pkts": cum_rx_pkts,
                "tx_pkts": cum_tx_pkts,
                "rx_bytes": cum_rx_bytes,
                "tx_bytes": cum_tx_bytes
            }

            # Update history (store the delta sample)
            if key not in self.stats_history:
                self.stats_history[key] = []
            self.stats_history[key].append({
                "timestamp": current_timestamp,
                "rx_pkts": delta_rx_pkts,
                "tx_pkts": delta_tx_pkts,
                "rx_bytes": delta_rx_bytes,
                "tx_bytes": delta_tx_bytes
            })
            # Keep only the most recent MAX_HISTORY samples
            if len(self.stats_history[key]) > MAX_HISTORY:
                self.stats_history[key].pop(0)
                
    # -------------------------------------------------------------------------
    # FFT-Based Prediction & QoS Decision (Every PREDICTION_SEGMENT_DURATION seconds)
    # -------------------------------------------------------------------------
    def _prediction_monitor(self):
        """
        Every PREDICTION_SEGMENT_DURATION seconds, for each switch, process the prediction for each port,
        extract a segment from the stored full prediction, compute the average predicted throughput,
        and then decide if QoS rules should be applied for that switch.
        """
        while True:
            hub.sleep(PREDICTION_SEGMENT_DURATION)
            current_time = datetime.now()
            # This dictionary will hold predictions grouped by switch (dpid)
            switch_predictions = {}

            for (dpid, port_no), history in self.stats_history.items():
                if len(history) < REQUIRED_HISTORY:
                    continue  # Not enough data

                # Get the port name for this port.
                port_name = self.port_names.get(dpid, {}).get(port_no, f"Port-{port_no}")
                # Construct the switch name based on dpid.
                switch_name = f"s{dpid}"
                # Skip excluded ports.
                if port_name in PORT_EXCLUDED.get(switch_name, []):
                    continue

                # Build the time series using the most recent REQUIRED_HISTORY samples.
                time_series = np.array([entry["rx_bytes"] for entry in history])
                port_key = (dpid, port_no)

                # Check the prediction cache.
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

                # Determine the sample offset based on elapsed time.
                elapsed = (current_time - prediction_time).total_seconds()
                sample_offset = int(elapsed / SLEEP_SEC)
                # Extract a segment corresponding to the next PREDICTION_SEGMENT_DURATION seconds.
                if sample_offset + PREDICTION_SEGMENT_SAMPLES <= len(full_prediction):
                    segment = full_prediction[sample_offset: sample_offset + PREDICTION_SEGMENT_SAMPLES]
                else:
                    segment = full_prediction[-PREDICTION_SEGMENT_SAMPLES:]
                # Convert predicted delta (bytes) into bits per second.
                predicted_bps_series = (segment * 8.0) / SLEEP_SEC
                avg_predicted_bps = np.mean(predicted_bps_series)

                self.logger.info(
                    "[Prediction Monitor] Switch: %s, Port: %s => Next %d sec mean: %.2f bps",
                    dpid, port_name, PREDICTION_SEGMENT_DURATION, avg_predicted_bps
                )

                # Group predictions by switch.
                if dpid not in switch_predictions:
                    switch_predictions[dpid] = {'affected': 0.0, 'others': 0.0, 'ports': []}
                # Save the prediction for debugging if needed.
                switch_predictions[dpid]['ports'].append((port_name, avg_predicted_bps))
                if port_name in PORT_AFFECTED.get(switch_name, []):
                    switch_predictions[dpid]['affected'] += avg_predicted_bps
                else:
                    switch_predictions[dpid]['others'] += avg_predicted_bps

            # Now, process each switch separately.
            for dpid, pred in switch_predictions.items():
                total_predicted_bps = pred['affected'] + pred['others']
                self.logger.info(
                    "[Prediction Summary] Switch: %s => Affected= %.2f bps, Others= %.2f bps, Total= %.2f bps, Ports: %s",
                    dpid, pred['affected'], pred['others'], total_predicted_bps, pred['ports']
                )
                
                # Define threshold: 90% of shared link capacity (converted to bps)
                threshold_bps_90 = 0.9 * (SHARED_LINK_BW * 1e6)
                threshold_bps_50 = 0.5 * (SHARED_LINK_BW * 1e6)

                if pred['others'] > threshold_bps_50:
                    self.logger.info(
                        "Switch: %s - Predicted throughput (%.2f bps) is over threshold (%.2f bps) -> Trigger QoS",
                        dpid, total_predicted_bps, threshold_bps_50
                    )
                    
                    pass 
                elif total_predicted_bps < threshold_bps_90:
                    self.logger.info(
                        "Switch: %s - Predicted throughput (%.2f bps) is below threshold (%.2f bps) -> Trigger QoS",
                        dpid, total_predicted_bps, threshold_bps_90
                    )
                    # Apply QoS for each affected port on this switch.
                    for (dpid_port, history) in self.stats_history.items():
                        dpid_curr, port_no = dpid_port
                        if dpid_curr != dpid:
                            continue
                        port_name = self.port_names.get(dpid, {}).get(port_no, f"Port-{port_no}")
                        if port_name in PORT_AFFECTED.get(f"s{dpid}", []):
                            self.apply_qos_rules(self.datapaths[dpid],
                                                 port_name,
                                                 0.5 * (SHARED_LINK_BW * 1e6))
                        else:
                            pass
                else:
                    self.logger.info(
                        "Switch: %s - Predicted throughput (%.2f bps) meets/exceeds threshold (%.2f bps) -> No QoS needed",
                        dpid, total_predicted_bps, threshold_bps_90
                    )
                    # Optionally: Remove previously applied QoS rules for this switch.

    # -------------------------------------------------------------------------
    # FFT Prediction with Zero-Padding + IFFT
    # -------------------------------------------------------------------------
    def predict_future_with_ifft(self, time_series, n_predict):
        """
        Predict future values by computing the FFT of the input time_series,
        zero-padding the FFT coefficients to length (n + n_predict) (n_predict should equal REQUIRED_HISTORY),
        and applying the IFFT.
        
        :param time_series: 1D numpy array of delta values (length n)
        :param n_predict: Number of extra samples to predict (should be 60)
        :return: 1D numpy array of predicted delta values (length n_predict)
        """
        n = len(time_series)
        fft_coeffs = np.fft.fft(time_series)
        # Optional: threshold small coefficients (remove noise)
        threshold = 0.25 * np.max(np.abs(fft_coeffs))
        fft_coeffs[np.abs(fft_coeffs) < threshold] = 0

        # Zero-pad FFT coefficients to new length: N = n + n_predict
        N = n + n_predict
        padded_fft = np.zeros(N, dtype=complex)
        half = (n // 2) + 1
        padded_fft[:half] = fft_coeffs[:half]
        padded_fft[-(n - half):] = fft_coeffs[half:]
        # Compute IFFT and scale appropriately.
        extended_signal = np.fft.ifft(padded_fft) * (N / n)
        return np.real(extended_signal[n:])  # Return only the extrapolated part

    # -------------------------------------------------------------------------
    # QoS Rule Application (Placeholder)
    # -------------------------------------------------------------------------
    def apply_qos_rules(self, datapath, port_name, guaranteed_bps):
        """
        Placeholder for QoS logic.
        :param datapath: The switch datapath object.
        :param port_name: Name of the port.
        :param guaranteed_bps: Guaranteed bandwidth (in bits per second).
        """
        mbps = guaranteed_bps / 1e6
        self.logger.info(
            "Applying QoS rules to DP: %s, Port: %s, guaranteeing bandwidth: %.2f Mbps",
            datapath.id, port_name, mbps
        )
        # TODO: Implement actual QoS configuration (e.g., queue setup, meter configuration).
