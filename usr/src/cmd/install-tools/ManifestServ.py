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

#
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#

# =============================================================================
# =============================================================================
"""
ManifestServ.py - ManifestServ XML data access interface module
                  commandline interface

"""
# =============================================================================
# =============================================================================

import errno
import sys

import atexit
import getopt
from osol_install.ManifestServ import ManifestServ

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def query_local(mfest_obj):
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    """ Process local (non-socket) queries from the commandline.

    Accepts queries from the commandline, and prints results to the
    screen.  Uses mfest_obj.get_values() to get the data.

    Loops until user types ^D.

    Args:
      mfest_obj: object to which queries are made for data.

    Returns: None

    Raises: None.

    """
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    key_mode = False
    print("")
    while (True):

        if (key_mode):
            print("Please enter a key or ")
            print(("\"-key\" to search an element or " +
                   "attribute path:"))
        else:
            print ("Please enter an element or attribute path")
            print("or \"+key\" to search for keys:")

        try:
            path = sys.stdin.readline()
            if not path:
                break
        except KeyboardInterrupt:
            break
        path = path.strip()

        if (path == "+key"):
            key_mode = True
            print("key mode set to " + str(key_mode))
            continue
        elif (path == "-key"):
            key_mode = False
            print("key mode set to " + str(key_mode))
            continue

        try:
            results = mfest_obj.get_values(path, key_mode)
        except Exception as err:
            print(("Exception caught when retrieving values"), file=sys.stderr)
            print("    request: " + path, file=sys.stderr)
            print("    " + str(err), file=sys.stderr)
            continue
			
        for result in results:
            if (result.strip() == ""):
                print("(empty string / no value)")
            else:
                print(result)


# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def usage(msg_fd):
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    """ Display commandline options and arguments.

    Args: file descriptor to write message to.

    Returns: None

    Raises: None

    """
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    print(("Usage: %s [-d] [-h|-?] [-s] [-t] [-v] " +
                      "[-f <validation_file_base> ]") % sys.argv[0], file=msg_fd)
    print(("    [-o <out_manifest.xml file> ] <manifest.xml file>"), file=msg_fd)
    print("where:", file=msg_fd)
    print(("  -d: turn on socket debug output (valid when " +
                      "-s also specified)"), file=msg_fd)
    print(("  -f <validation_file_base>: give basename " +
                      "for schema and defval files"), file=msg_fd)
    print(("      Defaults to basename of manifest " +
                      "(name less .xml suffix) when not provided"), file=msg_fd)
    print("  -h or -?: print this message", file=msg_fd)
    print(("  -o <out_manifest.xml file>: write resulting " +
                      "XML after defaults and"), file=msg_fd)
    print("      validation processing", file=msg_fd)
    print("  -t: save temporary file", file=msg_fd)
    print(("      Temp file is \"/tmp/" +
                      "<manifest_basename>_temp_<pid>"), file=msg_fd)
    print("  -v: verbose defaults/validation output", file=msg_fd)
    print(("  -s: start socket server for use by ManifestRead"), file=msg_fd)
    print(("  --dtd: use DTD validation (default is RelaxNG)"), file=msg_fd)


# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def exit_handler(mfest_obj):
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    """ Called at exit time to stop the socket server.

    Args:
      mfest_obj: ManifestServ object to stop the socket server on.

    Returns: None

    Raises: None

    """
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    mfest_obj.stop_socket_server()


# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
def main():
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    """ Main

    Args: None.  (Use sys.argv[] to get args)

    Returns: N/A

    Raises: None

    """
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    # Initialize option flags.
    d_flag = s_flag = t_flag = v_flag = dtd_flag = False

    mfest_obj = None
    err = None
    ret = 0

    # Options come first in the commandline.
    # See usage method for option explanations.
    try:
        (opt_pairs, other_args) = getopt.getopt(sys.argv[1:], "df:ho:stv?", "dtd")
    except getopt.GetoptError as err:
        print("ManifestServ: " + str(err), file=sys.stderr)
    except IndexError as err:
        print("ManifestServ: Insufficient arguments", file=sys.stderr)
    if (err):
        usage(sys.stderr)
        sys.exit (errno.EINVAL)

    valfile_root = None
    out_manifest = None
    for (opt, optarg) in opt_pairs:
        if (opt == "-d"):
            d_flag = True
        if (opt == "-f"):
            valfile_root = optarg
        if ((opt == "-h") or (opt == "-?")):
            usage(sys.stdout)
            sys.exit (0)
        if (opt == "-o"):
            out_manifest = optarg
        if (opt == "-s"):
            s_flag = True
        elif (opt == "-t"):
            t_flag = True
        elif (opt == "-v"):
            v_flag = True
        elif (opt == "--dtd"):
            dtd_flag = True

    # Must have the project data manifest.
    # Also check for mismatching options.
    if ((len(other_args) != 1) or (d_flag and not s_flag)):
        usage(sys.stderr)
        sys.exit (errno.EINVAL)

    try:
        # Create the object used to extract the data.
        mfest_obj = ManifestServ(other_args[0], valfile_root,
                                 out_manifest, v_flag, t_flag,
                                 dtd_schema=dtd_flag, socket_debug=d_flag)

        # Start the socket server if requested.
        if (s_flag):
            mfest_obj.start_socket_server()
            print("Connect to socket with name " + mfest_obj.get_sockname())

            # Set up to shut down the socket server at exit.
            atexit.register(exit_handler, mfest_obj)

        # Enable querying from this process as well.  This method will
        # block to hold the socket server open for remote queries as
        # well (if enabled).
        query_local(mfest_obj)
    except (SystemExit, KeyboardInterrupt):
        print("Caught SystemExit exception", file=sys.stderr)
    except Exception as err:
        print("Error running Manifest Server", file=sys.stderr)

    if (err is not None):
        ret = err.args[0]
        sys.exit(ret)

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# Main
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
if __name__ == "__main__":
    main()
