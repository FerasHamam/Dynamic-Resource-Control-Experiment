<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h3 align="center">Dynamic Resource Control Experiment</h3>
</p>


<!-- ABOUT THE EXPERIMENT -->
## About The Experiment

1. This experiment is aimed at enhancing the performance of data analysis by introducing a Quality of Service (QoS) method.
2. The QoS method utilizes dynamic control of network and storage resources based on the detected congestions.
3. It aims to improve overall data transfer efficiency and resource allocation in congested environments.
4. The experiment investigates how network congestion and storage bottlenecks impact data processing speed and accuracy.
5. The proposed method dynamically adjusts resources to maintain optimal performance during heavy network or storage load.

# ZeroMQ Client and Server Setup

Follow the steps below to install dependencies, build the project, and run the ZeroMQ client and server.

## 1. Install Dependencies

First, update your package lists and install the necessary dependencies:

```bash
sudo apt update
sudo apt install cmake
sudo apt install pkg-config
sudo apt install build-essential
sudo apt install libzmq3-dev

## 2. Build the client and server

Navigate to the client and server zmq directories
1. Navigate
a. Navigate in server
```bash
cd /path/to/zmqServer

b. Naviagte in client
```bash
cd /path/to/zmqServer

2. Create a build directory and navigate into it(client & server):
```bash
mkdir build
cd build

3. Run CMake to configure the project
```bash
cmake ..

4. Compile the client and server
```bash
make

After running these commands, the client and server executables will be built.

## 3. Run the client and Server

Server
```bash
./server

Client
```bash
./client


