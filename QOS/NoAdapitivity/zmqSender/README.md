<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Sender</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ sender.

## 1. Download the following
Go to this [link](https://drive.google.com/drive/folders/1EkHXA-k_TWk6JEP-0-5hSmtBPsAUsPXu?usp=share_link) and download full_data_xgc.bin.

From the sender side run the following:
```sh
mkdir /path/to/zmqSender/data/
```
from your end run the following
```sh
scp /path/to/file/full_data_xgc.bin server-ip:/path/to/zmqSender/data/
```

## 2. Give execute permissions to all scripts
```sh
cd /path/to/zmqSender/
chmod +x scripts/*
```

## 3. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo ./setup.sh
```

## 4. Modify the following

sender.c : line 14
```sh
#define SHARED_IP "Dedicated Receiver Interface IP"
```

sender.c : line 15
```sh
#define DETICATED_IP "Shared Receiver Interface IP"
```

sender.c : line 221 (1 will send through shared, 0 will send through dedicated)
```sh
args1.thread_index = 1 or 0;
```


## 5. Build the sender
```sh
cd /path/to/zmqSender
mkdir build
cd build
```
Run CMake to configure the project
```sh
cmake ..
```
Compile the sender c code
```sh
make
```

## 6. Run the Sender

Sender
```sh
./sender
```
