#!/bin/bash

make umount-dev
sudo rmmod block_lvl_dev
make clean
