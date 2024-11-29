<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Client</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ client.

## 1. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo apt update
sudo apt install cmake
sudo apt install pkg-config
sudo apt install build-essential
sudo apt install libzmq3-dev
```

## 2. Build the client
```sh
cd /path/to/zmqClient
mkdir build
cd build
```
Run CMake to configure the project
```sh
cmake ..
```
Compile the client c code
```sh
make
```

## 3. Run the Client

Client
```sh
./client
```
