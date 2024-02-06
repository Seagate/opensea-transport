# External Includes Notes and Structure

This readme describes how the external includes are layed out and their purpose.

## Folder Structure

Folders exist for RAID headers or operating system specific headers.

RAID folders may have subfolders for specific operating systems since RAID IOCTLs and structures may vary between implementations.

If a given RAID implementation's header file can be included from a accessible directory on the system, it should be preferred
over a copied file. For example, Linux's CCISS files can be found in /usr/include/linux and included from there. We do not need
to keep a checked-in copy with this project in this case since the Linux files may have a GPL license.
This is not necessary for BSD and similar licenses and including these is not an issue.
This will likely vary between RAID drives being supported since it will vary which ones are available in a given OS and where
they are available from.

Note files may be in certain folders to describe where headers may have originated in case they need to be updated, reviewed, etc.

## Current Structure

📦external
 ┣ 📂ciss
 ┃ ┣ 📂freebsd
 ┃ ┃ ┣ 📜NOTE
 ┃ ┃ ┣ 📜cissio.h
 ┃ ┃ ┗ 📜smartpqi_ioctl.h
 ┃ ┗ 📂solaris
 ┃ ┃ ┣ 📜NOTE
 ┃ ┃ ┣ 📜cpqary3.h
 ┃ ┃ ┣ 📜cpqary3_ciss.h
 ┃ ┃ ┗ 📜cpqary3_ioctl.h
 ┣ 📂csmi
 ┃ ┗ 📜csmisas.h
 ┗ 📜README.md

(Generated using file-tree-generator in VSCode)

## Using External Includes

To use the header files in this directory in other code, make sure to use the full include path.

Example:

    #include "external/ciss/freebsd/cissio.h"