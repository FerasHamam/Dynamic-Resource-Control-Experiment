#!/bin/bash

set -e

VENV_DIR="venv"

# Check if Python 3 is installed
if ! command -v python3 &> /dev/null; then
    echo "Python 3 is not installed. Installing Python 3..."
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if ! command -v brew &> /dev/null; then
            echo "Homebrew is not installed. Installing Homebrew..."
            /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        fi
        echo "Installing Python 3 using Homebrew..."
        brew install python
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux
        echo "Installing Python 3 using the package manager..."
        sudo apt update && sudo apt install -y python3 python3-venv
    else
        echo "Unsupported operating system. Please install Python 3 manually."
        exit 1
    fi
else
    echo "Python 3 is already installed."
fi

if [ ! -f "requirements.txt" ]; then
    echo "Error: requirements.txt not found!"
    exit 1
fi

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment..."
    python3 -m venv "$VENV_DIR"
else
    echo "Virtual environment already exists."
fi

echo "Activating virtual environment..."
source "$VENV_DIR/bin/activate"

echo "Upgrading pip..."
pip install --upgrade pip

echo "Installing packages from requirements.txt..."
pip install -r requirements.txt

echo "Deactivating virtual environment..."
deactivate

echo "Setup complete. Use 'source $VENV_DIR/bin/activate' to activate the virtual environment."