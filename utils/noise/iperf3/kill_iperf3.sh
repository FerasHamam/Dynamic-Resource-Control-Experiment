#!/bin/bash

pgrep -f periodic_noise.sh | xargs kill && pkill iperf3