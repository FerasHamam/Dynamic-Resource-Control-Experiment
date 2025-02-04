#!/bin/bash

# Delete filters
sudo tc filter del dev enp9s0

# Delete classes
sudo tc class del dev enp9s0 classid 1:10
sudo tc class del dev enp9s0 classid 1:20

# Delete qdisc
sudo tc qdisc del dev enp9s0 root


# Root qdisc
sudo tc qdisc add dev enp9s0 root handle 1: htb default 20

sudo tc class add dev enp9s0 parent 1: classid 1:1 htb rate 200mbit ceil 200mbit

sudo tc class add dev enp9s0 parent 1:1 classid 1:10 htb rate 100mbit ceil 200mbit

sudo tc class add dev enp9s0 parent 1:1 classid 1:20 htb rate 100mbit ceil 100mbit

sudo tc filter add dev enp9s0 protocol ip parent 1:0 u32 match ip dst 10.10.10.3 flowid 1:10

sudo tc filter add dev enp9s0 protocol ip parent 1:0 u32 match ip dst 10.10.10.4 flowid 1:20

sudo tc filter add dev enp9s0 protocol ip parent 1:0 u32 match ip dst 10.10.10.8 flowid 1:20