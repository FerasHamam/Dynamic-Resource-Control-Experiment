<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Server</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ server.

## 1. Download the following
Go to this [link](https://drive.google.com/drive/folders/1EkHXA-k_TWk6JEP-0-5hSmtBPsAUsPXu?usp=share_link) and download all the files.

From the server side run the following:
```sh
mkdir /path/to/zmqServer/data/
```
from your end run the following
```sh
scp /path/to/files/* server-ip:/path/to/zmqServer/data/
```

## 2. Give execute permissions to all scripts
```sh
cd /path/to/zmqServer/
chmod +x scripts/*
```

## 3. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo ./setup.sh
```

## 4. Build the server
```sh
cd /path/to/zmqServer
mkdir build
cd build
```
Run CMake to configure the project
```sh
cmake ..
```
Compile the server c code
```sh
make
```

## 5. Run the Server

Server
```sh
./server
```
