#!/bin/bash

pgrep -f run_periodic_noise_size.sh | xargs kill && pgrep -f run_periodic_sender.sh | xargs kill && pkill iperf3
pgrep -f run_periodic_sender.sh | xargs kill && pgrep -f run_periodic_sender.sh | xargs kill && pkill iperf3