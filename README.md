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

Installation
--------------
Show disk names

    sysctl hw.disknames

Create partition

    gpt create wd0
    gpt add -a 2m -s 512m -t efi wd0
    gpt add -a 2m -s 3g -t ffs wd0

Format partitions

    dkctl wd0 listwedges
    newfs_msdos -F 16 /dev/rdk0
    newfs -O 2 -V2 -f 2048 /dev/rdk1

Mount partitions
    
    mkdir /mnt/boot
    mkdir /mnt/root
    mount /dev/rdk0 /mnt/boot
    mount /dev/rdk1 /mnt/root


Development
--------------

    git clone https://github.com/oscar0pavon/pbsd.git

