#!/bin/sh

while true; do
    read -p "Have you updated the version number in setup.py and pynmrstar/__init__.py? " yn
    case $yn in
        [Yy]* ) pandoc -o README.rst README.md; python3 setup.py sdist upload --sign; break;;
        [Nn]* ) exit;;
        * ) echo "Please answer yes or no.";;
    esac
done