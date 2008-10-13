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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Manifest file node path definitions.
#
DISTRO_NAME = "name"
DISTRO_PARAMS = "distro_constr_params"
IMG_PARAMS = "img_params"
DISTRO_FLAGS = DISTRO_PARAMS + "/distro_constr_flags"
STOP_ON_ERR = DISTRO_FLAGS + "/stop_on_error"
CHECKPOINT_ENABLE = DISTRO_FLAGS + "/checkpoint_enable"
CHECKPOINT_RESUME = CHECKPOINT_ENABLE + "/resume_from"
DEFAULT_REPO = DISTRO_PARAMS + "/pkg_repo_default_authority"
DEFAULT_MAIN =  DEFAULT_REPO + "/main/"
DEFAULT_MAIN_AUTHNAME = DEFAULT_MAIN + "/authname"
DEFAULT_MAIN_URL = DEFAULT_MAIN + "/url"
DEFAULT_MIRROR = DEFAULT_REPO + "/mirror"
DEFAULT_MIRROR_URL = DEFAULT_MIRROR + "/url"
ADD_AUTH = DISTRO_PARAMS + "/pkg_repo_addl_authority"
ADD_AUTH_MAIN = ADD_AUTH + "/main"
ADD_AUTH_MAIN_AUTHNAME = ADD_AUTH_MAIN + "/authname"
ADD_AUTH_MAIN_URL = ADD_AUTH_MAIN + "/url"
ADD_AUTH_MIRROR = ADD_AUTH + "/mirror"
ADD_AUTH_MIRROR_URL = ADD_AUTH_MIRROR + "/url"
POST_INSTALL_DEFAULT = DISTRO_PARAMS + "/post_install_repo_default_authority" 
POST_INSTALL_DEFAULT_MAIN = POST_INSTALL_DEFAULT + "/main"
POST_INSTALL_DEFAULT_URL = POST_INSTALL_DEFAULT_MAIN + "/url" 
POST_INSTALL_DEFAULT_AUTH = POST_INSTALL_DEFAULT_MAIN + "/authname" 
POST_INSTALL_DEFAULT_MIRROR_URL = POST_INSTALL_DEFAULT + "/mirror/url"
POST_INSTALL_ADD_AUTH = DISTRO_PARAMS + "/post_install_repo_addl_authority"
POST_INSTALL_ADD_AUTH_MAIN = POST_INSTALL_ADD_AUTH + "/main"
POST_INSTALL_ADD_AUTH_URL = POST_INSTALL_ADD_AUTH_MAIN + "/url"
POST_INSTALL_ADD_AUTH_AUTH = POST_INSTALL_ADD_AUTH_MAIN + "/authname"
PKGS_TO_INSTALL = IMG_PARAMS + "/packages"
PKGS_TO_UNINSTALL = IMG_PARAMS + "/post_install_remove_packages"
PKG_NAME = "/pkg/name"
PKG_ATTRS = "/pkg/attrs"
PKG_TAGS = "/pkg/tags"
PKG_NAME_INSTALL = PKGS_TO_INSTALL + PKG_NAME
PKG_NAME_UNINSTALL = PKGS_TO_UNINSTALL + PKG_NAME
PKG_ATTRS_INSTALL =  PKGS_TO_INSTALL + PKG_ATTRS
PKG_ATTRS_UNINSTALL =  PKGS_TO_UNINSTALL + PKG_ATTRS
PKG_TAGS_INSTALL =  PKGS_TO_INSTALL + PKG_TAGS
PKG_TAGS_UNINSTALL =  PKGS_TO_UNINSTALL + PKG_TAGS
BOOT_ROOT_CONTENTS = IMG_PARAMS + "/bootroot_contents"
BOOT_ROOT_CONTENTS_BASE_INCLUDE = BOOT_ROOT_CONTENTS + "/base_include" 
BOOT_ROOT_CONTENTS_BASE_EXCLUDE = BOOT_ROOT_CONTENTS + "/base_exclude" 
BOOT_ROOT_CONTENTS_BASE_INCLUDE_TO_TYPE_DIR = BOOT_ROOT_CONTENTS_BASE_INCLUDE + "[type=\"dir\"]"
BOOT_ROOT_CONTENTS_BASE_EXCLUDE_TO_TYPE_DIR = BOOT_ROOT_CONTENTS_BASE_EXCLUDE + "[type=\"dir\"]"
BOOT_ROOT_CONTENTS_BASE_INCLUDE_TO_TYPE_FILE = BOOT_ROOT_CONTENTS_BASE_INCLUDE + "[type=\"file\"]"
OUTPUT_IMAGE = IMG_PARAMS + "/output_image"
OUTPUT_IMAGE_BOOTROOT = OUTPUT_IMAGE + "/bootroot"
BOOT_ROOT_COMPRESSION_TYPE = OUTPUT_IMAGE_BOOTROOT + "/compression/type"
BOOT_ROOT_COMPRESSION_LEVEL = OUTPUT_IMAGE_BOOTROOT + "/compression/level"
BOOT_ROOT_SIZE_PAD = OUTPUT_IMAGE_BOOTROOT + "/size_pad_mb"
COMPRESSION_TYPE = IMG_PARAMS + "/live_img_compression/type"
COMPRESSION_LEVEL = IMG_PARAMS + "/live_img_compression/level"
BUILD_AREA = IMG_PARAMS + "/build_area"
USER = IMG_PARAMS + "/user"
USER_UID = USER + "/UID"
LOCALE_LIST = IMG_PARAMS + "/locale_list"
FINALIZER_SCRIPT = OUTPUT_IMAGE + "/finalizer/script"
FINALIZER_SCRIPT_NAME = FINALIZER_SCRIPT + "/name"
FINALIZER_SCRIPT_ARGS = FINALIZER_SCRIPT + "/argslist"
FINALIZER_SCRIPT_NAME_TO_CHECKPOINT_MESSAGE = FINALIZER_SCRIPT + "[name=\"%s\"]/checkpoint/message"
FINALIZER_SCRIPT_NAME_TO_CHECKPOINT_NAME = FINALIZER_SCRIPT + "[name=\"%s\"]/checkpoint/name"
ADD_AUTH_URL_TO_AUTHNAME = ADD_AUTH_MAIN + "[url=\"%s\"]/authname"
ADD_AUTH_URL_TO_MIRROR_URL = ADD_AUTH_MAIN + "[url=\"%s\"]/../mirror/url"
POST_INSTALL_ADD_URL_TO_AUTHNAME = POST_INSTALL_ADD_AUTH_MAIN + "[url=\"%s\"]/authname"
POST_INSTALL_ADD_URL_TO_MIRROR_URL = POST_INSTALL_ADD_AUTH_MAIN + "[url=\"%s\"]/../mirror/url"
FINALIZER_SCRIPT_NAME_TO_ARGSLIST = FINALIZER_SCRIPT + "[name=\"%s\"]/argslist"

# Grub menu stuff
GRUB_DATA = IMG_PARAMS + "/grub_menu_modifications"
GRUB_DEFAULT_ENTRY_NUM = GRUB_DATA + "/default_entry"
GRUB_DEFAULT_TIMEOUT = GRUB_DATA + "/timeout"
GRUB_ENTRY = GRUB_DATA + "/entry"
GRUB_ENTRY_TITLE_SUFFIX = GRUB_ENTRY + "/title_suffix"
GRUB_ENTRY_POSITION = GRUB_ENTRY + "[title_suffix=\"%s\"]/position"
GRUB_ENTRY_LINES = GRUB_ENTRY + "[title_suffix=\"%s\"]/line"

FUTURE_URL = "http://pkg.opensolaris.org:80"
FUTURE_AUTH = "opensolaris.org"

#
# Path to the DC-manifest.rng and DC-manifest.defval.xml file.
# This is NOT the manifest user provides to building their images.
#
DC_MANIFEST_DATA="/usr/share/distro_const/DC-manifest"

FINALIZER_ROLLBACK_SCRIPT="/usr/share/distro_const/finalizer_rollback.py"
FINALIZER_CHECKPOINT_SCRIPT="/usr/share/distro_const/finalizer_checkpoint.py"

#
# Build area directory structure definitions. We will create
# subdirectories in the build area such that when completed we
# have <build_area>/build_data, <build_area>/build_data/pkg_image,
# <build_area>/build_data/tmp, <build_area>/build_data/bootroot,
# <build_area>/media, and <build_area>/logs
#
BUILD_DATA = "/build_data"
PKG_IMAGE = BUILD_DATA + "/pkg_image"
TMP = BUILD_DATA + "/tmp"
BOOTROOT = BUILD_DATA + "/bootroot"
MEDIA = "/media"
LOGS = "/logs"

# boot root definitions
BR_FILENAME = "/boot/x86.microroot"
DC_LOGGER_NAME="dc_logger"
