#!/bin/bash

echo "Cleaning up files and directories..."

# Step 1: Clean up the build and generated files
make clean
fusermount -u mnt 2>/dev/null || umount -l mnt 2>/dev/null
rm -rf mnt disk1 disk2 disk3

# Step 2: Rebuild and set up the environment
./cd.sh 3 disk
mkdir mnt
make all
./mkfs -r 0 -d disk1 -d disk2 -d disk3 -i 32 -b 200
./wfs disk1 disk2 disk3 -f -s mnt

echo "All steps completed successfully!"
