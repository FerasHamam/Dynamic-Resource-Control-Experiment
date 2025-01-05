#!/bin/bash

echo "Setup for Server started..."

# Update package list
sudo apt-get update

# Install required packages
sudo apt-get install -y build-essential libzmq3-dev cmake pkg-config

echo "Setup for Server is complete..."
