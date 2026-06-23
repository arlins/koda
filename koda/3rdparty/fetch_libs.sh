#!/bin/bash
# chmod +x fetch.sh

# 1. Install dependencies
echo "Checking dependencies..."
python3 -m pip install requests --quiet

# 2. Run fetcher
echo "Starting fetcher..."
python3 libs-fetcher.py

echo "Done. Press any key to exit..."
read -n 1