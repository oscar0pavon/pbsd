pBSD
======

Operating System based on NetBSD

Building
--------

You can cross-build NetBSD from most UNIX-like operating systems.

    ./build.sh -U -u -j32 -m amd64 release

With 32 thread the time of building is 14 minutes

Creating the usb image:

    ./build.sh -U -u -j32 -m amd64 install-image

Step by step:
Build time is messure with 32 threads

Tools:

    ./build.sh -U -u -j32 -m amd64 tools

Took 4 min

Bootloader:
    
    cd sys/arch/i386/stand/efiboot/bootx64
    mkdir obj
    ${TOOLS}/bin/nbmake MACHINE_ARCH=x86_64 MACHINE=amd64 -j32

Kernel:

    ./build.sh -U -u -j32 -m amd64 kernel=MYKERNEL

Took 1 min


Development
--------------

    git clone https://github.com/oscar0pavon/pbsd.git

