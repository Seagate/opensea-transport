# opensea-transport
Cross platform library containing common set of functions to issue standard commands to storage devices. 

Overview 
--------
The opensea-transport library has common set of defines and functions
that allow upper layers to send standard ATA, SCSI & NVMe commands to
storage devices connected through SATA, SAS, PCIe or USB interfaces.
This includes support for SATA devices attached to SAS HBAs.

The transport layer encapsulates the nuances of OS specific structures
by exposing a common API to send a command set to a particular device. 

Source
------

Building
--------
opensea-transport depends on the opensea-common library and the two should
be cloned to the same folder for Makefiles to build the libraries. 

All Makefile and Visual Studio project & solution files are part of Make folder.

The following will build the debug version of the library by default.

cd Make/gcc
make 

To build under Microsoft Windows, open the correspoinding 
Visual Studio Solution files for VS 2013 or 2015

Documentation
-------------
Header files & functions have doxygen documentation. 

Platforms
---------
Under Linux this libraries can be built on the following platforms using 
a cross platform compiler: 

        aarch64
        alpha 
        arm 
        armhf 
        hppa 
        m68k 
        mips 
        mips64 
        mips64el
        mipsel 
        powerpc 
        powerpc64 
        powerpc64le
        s390x 
        sh4 
        x86 
        x86_64 
        
This project can be build under Windows Visual Studio 2013 & 2015 solution
files for x86 and x64 targets. 
