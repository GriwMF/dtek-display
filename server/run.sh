#!/bin/bash
# Simple script to run the DTEK schedule scraper

cd "$(dirname "$0")"
source venv/bin/activate
python main.py

