Lenovo A7000-a Kernel Source
==============

Basic   | Spec Sheet
-------:|:-------------------------
CPU     | 1.5GHz 64-bit Octa-Core MT6752
GPU     | Mali-T760MP2
Memory  | 2GB RAM
Shipped Android Version | 5.0.2
Storage | 8GB
Display | 5.5" IPS 1280 x 720 px
Camera  | 8MPx, LED Flash

* Description

        */ The kernel is based off Open Source Code provided by Lenovo 
           Support for Lenovo A7000-a Smartphone (http://goo.gl/cPMNKb)
           
           Additional Features added will be commited seperately as a
           patch or will be included in upstream.
           
           Currenty, this tree is on top of latest patches for kernel-3.10
           provided by The Linux Kernel Organization (https://www.kernel.org/)
           
           Maintained by Rohan Taneja (c) 2016
        */

* Compilation
        
        $ export CROSS_COMPILE=aarch64-linux-android-

        $ export PATH=~/toolchains/aarch64-linux-android-4.9/bin:$PATH

        $ export ARCH=arm64

        $ make aio_row_defconfig ARCH=arm64 CROSS_COMPILE=aarch64-linux-android-

        $ make ARCH=arm64 CROSS_COMPILE=aarch64-linux-android-


