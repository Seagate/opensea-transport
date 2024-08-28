# opensea-transport

## Cross platform library for sending commands to storage devices through various operating systems, HBAs, and adapters.

### Copyright (c) 2014-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved

Welcome to opensea-transport, part of the openSeaChest open source project!
You can find the openSeaChest project [here](https://github.com/Seagate/openSeaChest).

### The opensea libraries

**opensea-common**      - Operating System common operations, not specific to
                      storage standards. Contains functions and defines that
                      are useful to all other libraries.

**opensea-transport**   - Contains standard ATA/SCSI/NVMe functions based on open
                      standards for these command sets.  This layer also
                      supports different transporting these commands through
                      operating systems to the storage devices. Code depends on
                      opensea-common.

**opensea-operations**  - Contains common use cases for operations to be performed
                      on a storage device. This layer encapsulates the nuances
                      of each command set (ATA/SCSI) and operating systems
                      (Linux/Windows etc.) Depends on opensea-common and
                      opensea-transport.

### Source

Source code for opensea-transport is available in this repo at [https://github.com/Seagate/opensea-transport](https://github.com/Seagate/opensea-transport).

### Building

See [BUILDING.md](BUILDING.md) for information on how to build the openSeaChest tools on Windows, Linux, FreeBSD, and Solaris/Illumos.

### Contributions & Issues

See [CONTRIBUTING.md](CONTRIBUTING.md) for more information on contributions that will be accepted.
This document also describes how to create an issue, generate a pull request, and licenses that will be accepted.

### Security policy

See [SECURITY.md](SECURITY.md) for information on Seagate's security policy for details on how to report security vulnerabilities.

### Names, Logos, and Brands

All product names, logos, and brands are property of their respective owners.
All company, product and service names mentioned in the source code are for
clarification purposes only. Use of these names, logos, and brands does not
imply endorsement.

### Support and Open Source Statement

Support from Seagate Technology for open source projects is different than traditional Technical Support.  If possible, please use the **Issues tab** in the individual software projects so that others may benefit from the questions and answers.  Include the output of --version information in the message. See the user guide section 'General Usage Hints' for information about saving output to a log file.

If you need to contact us through email, please choose one of these
two email addresses:

- opensource@seagate.com   for general questions and bug reports
- opensea-build@seagate.com   for specific questions about programming and building the software

Seagate offers technical support for drive installation.  If you have any questions related to Seagate products and technologies, feel free to submit your request on our web site. See the web site for a list of world-wide telephone numbers.

- [http://www.seagate.com/support-home/](http://www.seagate.com/support-home/)
- Contact Us:
[http://www.seagate.com/contacts/](http://www.seagate.com/contacts/)

This software uses open source packages obtained with permission from the
relevant parties. For a complete list of open source components, sources and
licenses, please see our Linux USB Boot Maker Utility FAQ for additional
information.

The newest online version of the openSeaChest Utilities documentation, open
source usage and acknowledgement licenses, and our Linux USB Boot Maker FAQ can
be found at: [https://github.com/Seagate/openSeaChest](https://github.com/Seagate/openSeaChest).

Copyright (c) 2014-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved

### License

BINARIES and SOURCE CODE files of the openSeaChest open source project have
been made available to you under the Mozilla Public License 2.0 (MPL).  Mozilla
is the custodian of the Mozilla Public License ("MPL"), an open source/free
software license.

The license can be views at [https://www.mozilla.org/en-US/MPL/](https://www.mozilla.org/en-US/MPL/) or [LICENSE.md](LICENSE.md)
