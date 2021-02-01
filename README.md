# FLIR-GTK

GTK+ application for FLIR ONE USB thermal camera based on flir-v4l:
  Copyright (C) 2015-2016 Thomas <tomas123 @ EEVblog Electronics Community Forum>
  https://github.com/fnoop/flirone-v4l2


== depdendencies ==
GTK+-3.0
Cairo
libusb-1.0
libjpeg
libcjson

This should install everything under Debian and derivatives:

apt install libgtk-3-dev libjpeg-dev libusb-1.0-0-dev libcjson-dev

== building ==
Makefile relies on pkg-config, if setup correctly simply running 'make'
should build the application.

== libusb & udev ==
cp 77-flirone-lusb.rules /lib/udev/rules.d/
udevadm control --reload-rules
