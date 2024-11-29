<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Server</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ server.

## 1. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo apt update
sudo apt install cmake
sudo apt install pkg-config
sudo apt install build-essential
sudo apt install libzmq3-dev
```

## 2. Download the following
```sh
cd /path/to/zmqServer/
mkdir data
cd data
wget https://drive.google.com/file/d/1WNoL5iFOPseMPzpClz_zh1ZrOyEzLX9Z/view?usp=sharing
wget https://drive.google.com/file/d/14zH0DN-vdwoWsJVIaVoPN-D0rq9kVGqk/view?usp=sharing
wget https://drive.google.com/file/d/1Se8CultEFrVKjKXVi7Eu0auprnJE4y20/view?usp=sharing
wget https://drive.google.com/file/d/1__eWVIw2FTMiNrrmhYGQUrQqNiLxGSKF/view?usp=sharing
wget https://drive.google.com/file/d/1gLeZFYXLgJimnJOIoutce-Sln2Pr9Ra2/view?usp=sharing
```

## 3. Give execute permissions to all scripts
```sh
cd /path/to/zmqServer/
chmod +x scripts/*
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
