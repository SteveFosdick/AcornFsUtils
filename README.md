# Acorn Filing System Utilities

## Introduction
This is a set of utilities to work with the Acorn filing systems DFS and
the 8-bit version of ADFS.  The programs can work with simple disc image
files that contain these filing systems and, on Linux where devices are
accessible as files, on actual devices.

The utilities auto-detect between ADFS and DFS and are also able to work
with disc image files from IDE emulations that have zero bytes between
the real data bytes - this is also detected automatically.

## Usage Summary
This is modelled after similar programs like cpmtools and dosfstools so
files on the host (PC) are referred to by a simple name and files inside
an Acorn filing system image are referred to with a name like:

*img-file*:*acorn-filename*

i.e. the name before the colon is the name of a disk image file or device
and the name after the colon is the name of a file within that disk image.

**afsls** <*img-file*[:*pattern*]> [...]

**afschk** <*img-file*>

**afscp** [ -r ] <*src*> [ <*src*>  ... ] <*dest*>

**afsmkdir** <*directory*> [ <*directory*> ... ]

**afstitle** <*img-file*> <*title*>

**acunzip** <*zip-file*> <...>

**scsi2ide** <*scsi-file*> <*ide-file*>

**ide2scsi** <*ide-file*> <*scsi-file*>
