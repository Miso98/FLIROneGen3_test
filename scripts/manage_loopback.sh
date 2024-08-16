#!/bin/bash
# Clear all existing loopback devices
sudo modprobe -r v4l2loopback

# Initialize 2 loopback devices
sudo modprobe v4l2loopback devices=2 video_nr=2,3
