#!/bin/bash

pgrep -f periodic_noise.sh | xargs kill && pkill iperf3

./periodic_noise.sh 10.10.10.4 4444 1.1G 220 & ./periodic_noise.sh 10.10.10.4 4445 256M 70

./periodic_noise.sh 10.10.10.8 4444 728M 150 & ./periodic_noise.sh 10.10.10.8 4445 128M 45