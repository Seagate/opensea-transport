# External Includes Notes and Structure

This readme describes how the external includes are layed out and their purpose.

## Folder Structure

Folders exist for RAID headers or operating system specific headers.

RAID folders may have subfolders for specific operating systems since RAID IOCTLs and structures may vary between implementations.

Note files may be in certain folders to describe where headers may have originated in case they need to be updated, reviewed, etc.

## Current Structure

📦external
 ┣ 📂ciss
 ┃ ┣ 📂freebsd
 ┃ ┃ ┣ 📜NOTE
 ┃ ┃ ┣ 📜cissio.h
 ┃ ┃ ┗ 📜smartpqi_ioctl.h
 ┃ ┣ 📂linux
 ┃ ┃ ┣ 📜NOTE
 ┃ ┃ ┣ 📜cciss_defs.h
 ┃ ┃ ┗ 📜cciss_ioctl.h
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

    #include "external/ciss/linux/cciss_ioctl.h"