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
# Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

'''
Represent all details of an installation in a single Python object,
including the disk target, netwrok configuration, system details (such as
timezone and locale), users, and zpool / zfs datasets.
'''

from osol_install.text_install.ti_install_utils import get_zpool_free_size

class InstallProfile(object):
    '''
    Represents an entire installation profile
    '''
    
    TAG = "install_profile"
    DEFAULT_BE_NAME = "openindiana"
    
    def __init__(self, disks=None, nic=None, system=None, users=None,
                 zpool_type=None, install_to_pool=False, pool_name=None):
        if disks is None:
            disks = []
        self.original_disks = []
        self.disks = disks
        self.nic = nic
        self.system = system
        if users is None:
            users = []
        self.users = users
        self.zpool_type = zpool_type
        self.install_to_pool = install_to_pool
        self.pool_name = pool_name
        self.be_name = InstallProfile.DEFAULT_BE_NAME
    
    def __str__(self):
        result = ["Install Profile:"]
        for disk in self.disks:
            result.append(str(disk))
        if self.install_to_pool and self.pool_name is not None:
            result.append("Pool: %s" % (self.pool_name))
        result.append(str(self.nic))
        result.append(str(self.system))
        for user in self.users:
            result.append(str(user))
        result.append(str(self.zpool_type))
        return "\n".join(result)

    def estimate_pool_size(self):
        pool_size = 0
        if len(self.disks) > 0:
            min_disk_size = (int) (self.disks[0].get_size().size_as("mb"))
            for disk in self.disks:
                disk_size = (int) (disk.get_size().size_as("mb"))
                if disk_size < min_disk_size:
                    min_disk_size = disk_size
            if self.zpool_type is None or self.zpool_type == 'mirror':
                pool_size = min_disk_size
            elif self.zpool_type == 'raidz' and len(self.disks) > 2:
                pool_size = min_disk_size * (len(self.disks) - 1)
            elif self.zpool_type == 'raidz2' and len(self.disks) > 3:
                pool_size = min_disk_size * (len(self.disks) - 2)
            elif self.zpool_type == 'raidz3' and len(self.disks) > 4:
                pool_size = min_disk_size * (len(self.disks) - 3)
        elif self.pool_name is not None:
            pool_size = get_zpool_free_size(self.pool_name)
        return pool_size
