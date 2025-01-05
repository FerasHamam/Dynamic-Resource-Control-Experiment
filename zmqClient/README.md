<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Client</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ client.

## 1. Give execute permissions to all scripts
```sh
cd /path/to/zmqClient/
chmod +x scripts/*
```

## 2. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo scripts/setup.sh
```

## 3. Build the client
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

## 4. Run the Client

Client
```sh
./client
```
