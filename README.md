# FLIR-GTK

GTK+ application for FLIR ONE USB thermal camera based on flir-v4l:
>  Copyright (C) 2015-2016 Thomas <tomas123 @ EEVblog Electronics Community Forum>

[https://github.com/fnoop/flirone-v4l2](https://github.com/fnoop/flirone-v4l2)

Update 2024 Mitchell (Miso98):

This code is adapted from the flirgtk repo developed by this person here https://source.dpin.de/nica/flir-gtk/-/blob/master/flirgtk.c The above repo is a cleaned and updated version of https://github.com/fnoop/flirone-v4l2 but now includes a very helpful GUI This code is meant to allow for the use of a FLIR One camera (which is connected via USB-C and only on Android OS) with a Linux Machine. However, I have modified it for the resolution of the FLIR One Gen 3 for android (80x60) and includes a hard scaling factor for noise attenuation to create less blue or red shifted images

The original author and contributor has since stopped updating the FLIR-GTK project as they have begun using a new camera but has since left the following message:
Just recently (July 2023) I got myself another IR camera and will not use my FLIR
One Pro anymore. I also can not afford to keep both. I could just for
hacking fun continue to work on this project but then I would need some
funding to recoup not selling the FLIR One.

So if you have an interest in keeping this FLIR-GTK project going, I'd be
happy for a donation, you can use my PayPal:
https://paypal.me/NFaerber42



## depdendencies
```
GTK+-3.0
Cairo
libusb-1.0
libjpeg
libcjson
```
This should install everything under Debian and derivatives:

apt install libgtk-3-dev libjpeg-dev libusb-1.0-0-dev libcjson-dev

## building
If you check out the code from git you first need to clone submodule cairo-jpeg:
    git submodule init
    git submodule update

Makefile relies on pkg-config, if setup correctly simply running 'make'
should build the application which can be run from the source directory,
'make deb' builds a Debian package (to the parent directory)

## libusb & udev
For access rights of the application to the USB device:

    cp 77-flirone-lusb.rules /lib/udev/rules.d/
    udevadm control --reload-rules






