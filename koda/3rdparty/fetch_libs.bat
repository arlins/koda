@echo off
setlocal

echo Checking dependencies...
python -m pip install requests

echo Starting fetcher...
python libs-fetcher.py
:: python libs-fetcher.py --config libs-config.json --output ./

echo Done.
pause