#!/usr/bin/python3.5
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#

"""loader_setup
Customizations to the loader menu.

 To be done before post_bootroot_pkg_image_mod gets called.

"""

import os
import sys
from osol_install.ManifestRead import ManifestRead
from osol_install.distro_const.dc_utils import get_manifest_value
from osol_install.distro_const.dc_utils import get_manifest_list
from osol_install.distro_const.dc_defs import LOADER_DEFAULT_TIMEOUT, \
	LOADER_MENU_FILES

DEFAULT_TIMEOUT = "10" # Seconds
CP = "/bin/cp"

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Main
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
""" Customizations to the grub menu.

Args:
  MFEST_SOCKET: Socket needed to get manifest data via ManifestRead object

  PKG_IMG_PATH: Package image area mountpoint

  TMP_DIR: Temporary directory (not used)

  BA_BUILD: Area where boot archive is put together.  (not used)

  MEDIA_DIR: Area where the media is put. (not used)

  LOADER_SETUP_TYPE: The type of image being created (ai or livecd)

Note: This assumes a populated pkg_image area exists at the location
        ${PKG_IMG_PATH}

"""
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if (len(sys.argv) != 7): # Don't forget sys.argv[0] is the script itself.
    raise Exception(sys.argv[0] + ": Requires 6 args:\n" +
    "    Reader socket, pkg_image area, temp dir,\n" +
    "    boot archive build area, media area, loader setup type.")

# collect input arguments from what this script sees as a commandline.
MFEST_SOCKET = sys.argv[1]      # Manifest reader socket
PKG_IMG_PATH = sys.argv[2]      # package image area mountpoint
LOADER_SETUP_TYPE = sys.argv[6]	# Grub setup type

# get the manifest reader object from the socket
MANIFEST_READER_OBJ = ManifestRead(MFEST_SOCKET)

# Get default timeout from manifest. if it exists.
TIMEOUT = get_manifest_value(MANIFEST_READER_OBJ, LOADER_DEFAULT_TIMEOUT)
if TIMEOUT is not None and TIMEOUT != DEFAULT_TIMEOUT:
	# Open loader.conf file.
	try:
	    LOADER_CONF_FILE = open(PKG_IMG_PATH + "/boot/loader.conf", "a")
	except IOError as err:
	    print("Error opening loader.conf for writing", file=sys.stderr)
	    raise

	LOADER_CONF_FILE.write("autoboot_delay=" + TIMEOUT + "\n")
	LOADER_CONF_FILE.close()

CMD = CP + " " + LOADER_MENU_FILES + "/* " + PKG_IMG_PATH + "/boot"

if (LOADER_SETUP_TYPE == "ai"):
	# The following entries are the standard "hardwired" entries for AI
	# To be implemented
	try:
	    os.system(CMD)
	except OSError:
	    print("WARNING: Error while copying loader files", file=sys.stderr)

elif (LOADER_SETUP_TYPE == "livecd"):
	# The following entries are the standard "hardwired" entries for livecd.
	try:
	    os.system(CMD)
	except OSError:
	    print("WARNING: Error while copying loader files", file=sys.stderr)

elif (LOADER_SETUP_TYPE == "text-install"):
	# The following entries are the standard "hardwired" entries for
	# the text-installer
	CMD = CP + " " + LOADER_MENU_FILES + "/loader.rc.local " + \
		PKG_IMG_PATH + "/boot"
	try:
	    os.system(CMD)
	except OSError:
	    print("WARNING: Error while copying loader files", file=sys.stderr)

sys.exit(0)
