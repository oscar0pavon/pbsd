pBSD
======

Operating System based on NetBSD

Building
--------

You can cross-build NetBSD from most UNIX-like operating systems.

    ./build.sh -U -u -j32 -m amd64 -O ./build release

With 32 thread the time of building is 14 minutes

Creating the usb image:

    ./build.sh -U -u -j32 -m amd64 -O ./build install-image


Development
--------------

    git clone https://github.com/oscar0pavon/pbsd.git

