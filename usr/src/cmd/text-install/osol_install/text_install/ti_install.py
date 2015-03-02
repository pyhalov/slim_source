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
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
#

'''
Installation engine for Text Installer
'''

import os
import logging
import commands
import datetime
import platform
import shutil
import subprocess as sp
import osol_install.tgt as tgt
from osol_install.libzoneinfo import tz_isvalid
from libbe_py import beUnmount
from osol_install.transfer_mod import tm_perform_transfer, tm_abort_transfer
from osol_install.transfer_defs import TM_ATTR_MECHANISM, \
    TM_PERFORM_CPIO, TM_CPIO_ACTION, TM_CPIO_ENTIRE, TM_CPIO_SRC_MNTPT, \
    TM_CPIO_DST_MNTPT, TM_UNPACK_ARCHIVE, TM_SUCCESS
from osol_install.install_utils import exec_cmd_outputs_to_log
from osol_install.profile.disk_info import PartitionInfo
from osol_install.profile.network_info import NetworkInfo
from osol_install.text_install import RELEASE
import osol_install.text_install.ti_install_utils as ti_utils 
from osol_install.ict import correct_rdsk

#
# RTC command to run
#
RTC_CMD = "/usr/sbin/rtc"

#
# Program used for calling the C ICT functions.
# When all the C-based ICT functions are converted to Python (bug 6256),
# this can be removed.
#
ICT_PROG = "/opt/install-test/bin/ict_test"


# The following is defined for using the ICT program.  It can be removed
# once the ict_test program is not used.
CPIO_TRANSFER = "0"

# The following 2 values, ICT_USER_UID and ICT_USER_GID are defined
# in the ICT C APIs.  When those are ported to Python, these will
# probably be defined there.
ICT_USER_UID = "101"
ICT_USER_GID = "10"

INSTALL_FINISH_PROG = "/sbin/install-finish"

# Initial BE name
INIT_BE_NAME = "openindiana"

# definitions for ZFS pool
INSTALL_SNAPSHOT = "install"

INSTALLED_ROOT_DIR = "/a"
X86_BOOT_ARCHIVE_PATH = "/.cdrom/platform/i86pc/%s/boot_archive"

# these directories must be defined in this order, otherwise,
# "zfs unmount" fails
ZFS_SHARED_FS = ["/export/home", "/export"]

#
# Handle for accessing the InstallStatus class
#
INSTALL_STATUS = None


class InstallStatus(object):
    '''Stores information on the installation progress, and provides a
    hook for updating the screen.
    
    '''
    TI = "ti"
    TM = "tm"
    ICT = "ict"

    def __init__(self, screen, update_status_func, quit_event):
        '''screen and update_status_func are values passed in from
        the main app
        
        '''

        # Relative ratio used for computing the overall progress of the
        # installation.  All numbers must add up to 100%
        self.ratio = {InstallStatus.TI:0.05,
                      InstallStatus.TM:0.93,
                      InstallStatus.ICT:0.02}

        self.screen = screen
        self.update_status_func = update_status_func
        self.quit_event = quit_event
        self.previous_step_name = None
        self.step_percent_completed = 0
        self.previous_overall_progress = 0

    def update(self, step_name, percent_completed, message):
        '''Update the install status. Also checks the quit_event to see
        if the installation should be aborted.
        
        '''
        if self.quit_event.is_set():
            logging.debug("User selected to quit")
            raise ti_utils.InstallationError
        if (self.previous_step_name is None):
            self.previous_step_name = step_name
        elif (self.previous_step_name != step_name):
            self.previous_step_name = step_name
            self.step_percent_completed = self.previous_overall_progress
        overall_progress = (percent_completed * (self.ratio[step_name])) \
                           + self.step_percent_completed
        self.update_status_func(self.screen, overall_progress, message)
        self.previous_overall_progress = overall_progress


SYSID_FILE = "/etc/.sysIDtool.state"
SYSID_FILE_UMASK = 022
SYSID_FILE_CONTENT = [["1", "# System previously configured?"],
              ["1", "# Bootparams succeeded?"],
              ["1", "# System is on a network?"],
              ["1", "# Extended network information gathered?"],
              ["1", "# Autobinder succeeded?"],
              ["1", "# Network has subnets?"],
              ["1", "# root password prompted for?"],
              ["1", "# locale and term prompted for?"],
              ["1", "# security policy in place"],
              ["1", "# NFSv4 domain configured"],
              # term type must be the last entry
              ["sun", ""]]


def create_sysid_file():
    '''Create the /etc/.sysIDtool.state file'''
    try:
        with open(SYSID_FILE, "w") as sysid_file:
            for entry in SYSID_FILE_CONTENT:
                sysid_file.write(entry[0] + "\t" + entry[1] + "\n")
  
        # set the umask on the file
        os.chmod(SYSID_FILE, SYSID_FILE_UMASK)
    except IOError, ioe:
        logging.error("Unable to write to %s", SYSID_FILE)
        logging.exception(ioe)
        raise ti_utils.InstallationError
    except OSError, ose:
        logging.error("Unable to create %s", SYSID_FILE)
        logging.exception(ose)
        raise ti_utils.InstallationError


def transfer_mod_callback(percent, message):
    '''Callback for transfer module to indicate percentage complete.'''
    logging.debug("tm callback: %s: %s", percent, message)
    try:
        INSTALL_STATUS.update(InstallStatus.TM, percent, message)
    except ti_utils.InstallationError:
        # User selected to quit the transfer
        tm_abort_transfer()

def exec_cmd(cmd, description):
    ''' Execute the given command.

        Args:
            cmd: Command to execute.  The command and it's arguments
		 should be provided as a list, suitable for used
		 with subprocess.Popen(shell=False)
            description: Description to use for printing errors.

        Raises:
            InstallationError
    
    '''
    logging.debug("Executing: %s", " ".join(cmd))
    if exec_cmd_outputs_to_log(cmd, logging) != 0:
        logging.error("Failed to %s", description)
        raise ti_utils.InstallationError

def cleanup_existing_install_target(install_profile, inst_device):
    ''' If installer was restarted after the failure, it is necessary
        to destroy the pool previously created by the installer.

        If there is a root pool manually imported by the user with
        the same name which will be used by the installer
        for target root pool, we don't want to destroy user's data.
        So, we will log warning message and abort the installation.

    '''

    # Umount /var/run/boot_archive, which might be mounted by
    # previous x86 installations.
    # Error from this command is intentionally ignored, because the
    # previous invocation of the transfer module might or might not have
    # mounted on the mount point.
    if platform.processor() == "i386":
        with open("/dev/null", "w") as null_handle:
            sp.Popen(["/usr/sbin/umount", "-f", "/var/run/boot_archive"],
                     stdout=null_handle, stderr=null_handle)

    rootpool_name = install_profile.disk.get_install_root_pool()

    cmd = "/usr/sbin/zpool list " + rootpool_name
    logging.debug("Executing: %s", cmd)
    status = commands.getstatusoutput(cmd)[0]
    if status != 0:
        logging.debug("Root pool %s does not exist", rootpool_name)
        return   # rpool doesn't exist, no need to clean up

    # Check value of rpool's org.openindiana.caiman:install property
    # If it is busy, that means the pool is left over from an aborted install.
    # If the property doesn't exist or has another value, we assume
    # that the root pool contains valid Solaris instance.
    cmd = "/usr/sbin/zfs get -H -o value org.openindiana.caiman:install " + \
          rootpool_name
    logging.debug("Executing: %s", cmd)
    (status, pool_status) = commands.getstatusoutput(cmd)
    logging.debug("Return code: %s", status)
    logging.debug("Pool status: %s", pool_status)
    if (status != 0) or (pool_status != "busy"):
        logging.error("Root pool %s exists.", rootpool_name)
        logging.error("Installation can not proceed")
        raise ti_utils.InstallationError

    try:
        rpool = tgt.Zpool(rootpool_name, inst_device)
        tgt.release_zfs_root_pool(rpool)
        logging.debug("Completed release_zfs_root_pool")
    except TypeError, te:
        logging.error("Failed to release existing rpool.")
        logging.exception(te)
        raise ti_utils.InstallationError

    # clean up the target mount point
    exec_cmd(["/usr/bin/rm", "-rf", INSTALLED_ROOT_DIR + "/*"],
             "clean up existing mount point")

def do_ti(install_profile, swap_dump):
    '''Call the ti module to create the disk layout, create a zfs root
    pool, create zfs volumes for swap and dump, and to create a be.

    '''
    diskname = install_profile.disk.name
    logging.debug("Diskname: %s", diskname)
    mesg = "Preparing disk for %(release)s installation" % RELEASE
    try:
        (inst_device, inst_device_size) = \
             install_profile.disk.get_install_dev_name_and_size()

        # The installation size we provide already included the required
        # swap size
        (swap_type, swap_size, dump_type, dump_size) = \
            swap_dump.calc_swap_dump_size(ti_utils.get_minimum_size(swap_dump),
                                          inst_device_size, swap_included=True)

        tgt_disk = install_profile.disk.to_tgt()
        tgt.create_disk_target(tgt_disk, False)
        logging.debug("Completed create_disk_target")
        INSTALL_STATUS.update(InstallStatus.TI, 20, mesg)

        rootpool_name = install_profile.disk.get_install_root_pool()
        rpool = tgt.Zpool(rootpool_name, inst_device)
        tgt.create_zfs_root_pool(rpool)
        logging.debug("Completed create_zfs_root_pool")
        INSTALL_STATUS.update(InstallStatus.TI, 40, mesg)

        create_swap = False
        if (swap_type == ti_utils.SwapDump.ZVOL):
            create_swap = True

        create_dump = False
        if (dump_type == ti_utils.SwapDump.ZVOL):
            create_dump = True

        logging.debug("Create swap %s Swap size: %s", create_swap, swap_size)
        logging.debug("Create dump %s Dump size: %s", create_dump, dump_size)

        tgt.create_zfs_volume(rootpool_name, create_swap, swap_size,
                              create_dump, dump_size)
        logging.debug("Completed create swap and dump")
        INSTALL_STATUS.update(InstallStatus.TI, 70, mesg)

        zfs_datasets = ()
        for ds in reversed(ZFS_SHARED_FS): # must traverse it in reversed order
            zd = tgt.ZFSDataset(mountpoint=ds)
            zfs_datasets += (zd,)
            logging.debug("Adding dataset ZFSDataset(%s %s %s %s %s %s %s)",
		zd.name, zd.mountpoint, zd.be_name, zd.zfs_swap, zd.swap_size,
		zd.zfs_dump, zd.dump_size)
	logging.debug("rootpol_name %s, init_be_name %s, INSTALLED_ROOT_DIR %s",
		rootpool_name,INIT_BE_NAME,  INSTALLED_ROOT_DIR)
        tgt.create_be_target(rootpool_name, INIT_BE_NAME, INSTALLED_ROOT_DIR,
                             zfs_datasets)

        logging.debug("Completed create_be_target")
        INSTALL_STATUS.update(InstallStatus.TI, 100, mesg)
    except TypeError, te:
        logging.error("Failed to initialize disk")
        logging.exception(te)
        raise ti_utils.InstallationError

def do_transfer():
    '''Call libtransfer to transfer the bits to the system via cpio.'''
    # transfer the bits
    tm_argslist = [(TM_ATTR_MECHANISM, TM_PERFORM_CPIO),
                   (TM_CPIO_ACTION, TM_CPIO_ENTIRE),
                   (TM_CPIO_SRC_MNTPT, "/"),
                   (TM_CPIO_DST_MNTPT, INSTALLED_ROOT_DIR)]

    # if it is running on x86, need to unpack the root archive from
    # the architecture that's not booted from.
    if platform.processor() == "i386":
        (status, inst_set) = commands.getstatusoutput("/bin/isainfo -k")
        if (status != 0):
            logging.error("Unable to determine instruction set.")
            raise ti_utils.InstallationError

        if (inst_set == "amd64"):
            # Running 64 bit kernel, need to unpack 32 bit archive
            tm_argslist.extend([(TM_UNPACK_ARCHIVE,
                               X86_BOOT_ARCHIVE_PATH % "")])
        else:
            # Running 32 bit kernel, need to unpack 64 bit archive
            tm_argslist.extend([(TM_UNPACK_ARCHIVE,
                               X86_BOOT_ARCHIVE_PATH % "amd64")])

    logging.debug("Going to call TM with this list: %s", tm_argslist)
    
    try:
        status = tm_perform_transfer(tm_argslist,
                                     callback=transfer_mod_callback)
    except Exception, ex:
        logging.exception(ex)
        status = 1

    if status != TM_SUCCESS:
        logging.error("Failed to transfer bits to the target")
        raise ti_utils.InstallationError

def do_ti_install(install_profile, screen, update_status_func, quit_event,
                       time_change_event):
    '''Installation engine for text installer.

       Raises InstallationError for any error occurred during install.

    '''
    #
    # The following information is needed for installation.
    # Make sure they are provided before even starting
    #

    # locale
    locale = install_profile.system.locale
    logging.debug("default locale: %s", locale)

    # timezone
    timezone = install_profile.system.tz_timezone
    logging.debug("time zone: %s", timezone)

    # hostname
    hostname = install_profile.system.hostname
    logging.debug("hostname: %s", hostname)

    ulogin = None 
    user_home_dir = ""

    root_user = install_profile.users[0]
    root_pass = root_user.password

    reg_user = install_profile.users[1]
    ureal_name = reg_user.real_name
    ulogin = reg_user.login_name
    upass = reg_user.password

    logging.debug("Root password: %s", root_pass)

    if ulogin:
        user_home_dir = "/export/home/" + ulogin
        ZFS_SHARED_FS.insert(0, user_home_dir)
        logging.debug("User real name: %s", ureal_name)
        logging.debug("User login: %s", ulogin)
        logging.debug("User password: %s", upass)

    (inst_device, inst_device_size) = \
              install_profile.disk.get_install_dev_name_and_size()
    logging.debug("Installation Device Name: %s", inst_device)
    logging.debug("Installation Device Size: %sMB", inst_device_size)

    swap_dump = ti_utils.SwapDump()

    min_inst_size = ti_utils.get_minimum_size(swap_dump)
    logging.debug("Minimum required size: %sMB", min_inst_size)
    if (inst_device_size < min_inst_size):
        logging.error("Size of device specified for installation "
                      "is too small")
        logging.error("Size of install device: %sMB", inst_device_size)
        logging.error("Minimum required size: %sMB", min_inst_size)
        raise ti_utils.InstallationError

    recommended_size = ti_utils.get_recommended_size(swap_dump)
    logging.debug("Recommended size: %sMB", recommended_size)
    if (inst_device_size < recommended_size):
        # Warn users that their install target size is not optimal
        # Just log the warning, but continue with the installation.
        logging.warning("Size of device specified for installation is "
                        "not optimal") 
        logging.warning("Size of install device: %sMB", inst_device_size)
        logging.warning("Recommended size: %sMB", recommended_size)

    # Validate the value specified for timezone
    if not tz_isvalid(timezone):
        logging.error("Timezone value specified (%s) is not valid", timezone)
        raise ti_utils.InstallationError

    # Compute the time to set here.  It will be set after the rtc
    # command is run, if on x86.
    install_time = datetime.datetime.now() + install_profile.system.time_offset
    
    if platform.processor() == "i386":
        #
        # At this time, the /usr/sbin/rtc command does not work in alternate
        # root.  It hard codes to use /etc/rtc_config.
        # Therefore, we set the value for rtc_config in the live environment
        # so it will get copied over to the alternate root.
        #
        exec_cmd([RTC_CMD, "-z", timezone], "set timezone")
        exec_cmd([RTC_CMD, "-c"], "set timezone")

    #
    # Set the system time to the time specified by the user
    # The value to set the time to is computed before the "rtc" commands.
    # This is required because rtc will mess up the computation of the
    # time to set.  The rtc command must be run before the command
    # to set time.  Otherwise, the time that we set will be overwritten
    # after running /usr/sbin/rtc.
    #
    cmd = ["/usr/bin/date", install_time.strftime("%m%d%H%M%y")]
    exec_cmd(cmd, "set system time")

    time_change_event.set()
    
    global INSTALL_STATUS
    INSTALL_STATUS = InstallStatus(screen, update_status_func, quit_event)

    rootpool_name = install_profile.disk.get_install_root_pool()

    cleanup_existing_install_target(install_profile, inst_device)

    do_ti(install_profile, swap_dump)

    do_transfer()

    ict_mesg = "Completing transfer process"
    INSTALL_STATUS.update(InstallStatus.ICT, 0, ict_mesg)

    # Save the timezone in the installed root's /etc/default/init file
    ti_utils.save_timezone_in_init(INSTALLED_ROOT_DIR, timezone)

    # If swap was created, add appropriate entry to <target>/etc/vfstab
    swap_device = swap_dump.get_swap_device(rootpool_name) 
    logging.debug("Swap device: %s", swap_device)
    ti_utils.setup_etc_vfstab_for_swap(swap_device, INSTALLED_ROOT_DIR)

    #
    # The /etc/.sysIDtool.state file needs to be written before calling
    # the ICTs, because it gets copied over by the ICTs into the installed
    # system.
    #
    create_sysid_file()
    
    try:
        run_ICTs(install_profile, hostname, ict_mesg, inst_device,
                 locale, root_pass, ulogin, upass, ureal_name,
                 rootpool_name)
    finally:
        post_install_cleanup(install_profile, rootpool_name)
    
    INSTALL_STATUS.update(InstallStatus.ICT, 100, ict_mesg)
    

def post_install_cleanup(install_profile, rootpool_name):
    '''Do final cleanup to prep system for first boot, such as resetting
    the ZFS dataset mountpoints
    
    '''
    # reset_zfs_mount_property
    # Setup mountpoint property back to "/" from "/a" for
    # /, /opt, /export, /export/home

    # make sure we are not in the alternate root.
    # Otherwise, be_unmount() fails
    os.chdir("/root")

    # since be_unmount() can not currently handle shared filesystems,
    # it's necesary to manually set their mountpoint to the appropriate value
    for fs in ZFS_SHARED_FS:
        exec_cmd(["/usr/sbin/zfs", "unmount", rootpool_name + fs],
                 "unmount " + rootpool_name + fs)
        exec_cmd(["/usr/sbin/zfs", "set", "mountpoint=" + fs,
                 rootpool_name + fs], "change mount point for " +
                 rootpool_name + fs)

    # Transfer the log file
    final_log_loc = INSTALLED_ROOT_DIR + install_profile.log_final
    logging.debug("Copying %s to %s", install_profile.log_location,
                  final_log_loc)
    try:
        shutil.copyfile(install_profile.log_location, final_log_loc)
    except (IOError, OSError), err: 
        logging.error("Failed to copy %s to %s", install_profile.log_location,
                      install_profile.log_final)
        logging.exception(err)
        raise ti_utils.InstallationError
        
    # 0 for the 2nd argument because force-umount need to be 0
    if beUnmount(INIT_BE_NAME, 0) != 0:
        logging.error("beUnmount failed for %s", INIT_BE_NAME)
        raise ti_utils.InstallationError

# pylint: disable-msg=C0103
def run_ICTs(install_profile, hostname, ict_mesg, inst_device, locale,
             root_pass, ulogin, upass, ureal_name, rootpool_name):
    '''Run all necessary ICTs. This function ensures that each ICT is run,
    regardless of the success/failure of any others. After running all ICTs
    (including those supplied by install-finish), if any of them failed,
    an InstallationError is raised.
    
    '''
    
    failed_icts = 0
    
    #
    # set the language locale
    #
    if (locale != ""):
        try:
            exec_cmd([ICT_PROG, "ict_set_lang_locale", INSTALLED_ROOT_DIR,
                      locale, CPIO_TRANSFER],
                      "execute ict_set_lang_locale() ICT")
        except ti_utils.InstallationError:
            failed_icts += 1

    #
    # create user directory if needed
    #
    try:
        exec_cmd([ICT_PROG, "ict_configure_user_directory", INSTALLED_ROOT_DIR,
                  ulogin], "execute ict_configure_user_directory() ICT")
    except ti_utils.InstallationError:
        failed_icts += 1

    #
    # set host name
    #
    try:
        exec_cmd([ICT_PROG, "ict_set_host_node_name",
                  INSTALLED_ROOT_DIR, hostname],
                  "execute ict_set_host_node_name() ICT")
    except ti_utils.InstallationError:
        failed_icts += 1
    
    try:
        exec_cmd([ICT_PROG, "ict_set_user_profile", INSTALLED_ROOT_DIR,
                  ulogin], "execute ict_set_user_profile() ICT")
    except ti_utils.InstallationError:
        failed_icts += 1

    # Setup bootfs property so that newly created Solaris instance is booted
    # appropriately
    initial_be = rootpool_name + "/ROOT/" + INIT_BE_NAME
    try:
        exec_cmd(["/usr/sbin/zpool", "set", "bootfs=" + initial_be,
                  rootpool_name], "activate BE")
    except ti_utils.InstallationError:
        failed_icts += 1
    
    is_logical = "0"
    part_info = install_profile.disk.get_solaris_data()
    if isinstance(part_info, PartitionInfo) and part_info.is_logical():
        is_logical = "1"
    
    try:
        exec_cmd([ICT_PROG, "ict_installboot", INSTALLED_ROOT_DIR,
                  correct_rdsk(inst_device),
                  is_logical], "execute ict_installboot() ICT")
    except ti_utils.InstallationError:
        failed_icts += 1

    INSTALL_STATUS.update(InstallStatus.ICT, 50, ict_mesg)

    # Run the install-finish script
    cmd = [INSTALL_FINISH_PROG, "-B", INSTALLED_ROOT_DIR, "-R", root_pass,
           "-n", ureal_name, "-l", ulogin, "-p", upass, "-G", ICT_USER_GID,
           "-U", ICT_USER_UID]
    if (install_profile.nic.type == NetworkInfo.NONE):
        cmd.append("-N")
    
    try:
        exec_cmd(cmd, "execute INSTALL_FINISH_PROG")
    except ti_utils.InstallationError:
        failed_icts += 1
    
    # Take a snapshot of the installation
    try:
        exec_cmd([ICT_PROG, "ict_snapshot", INIT_BE_NAME, INSTALL_SNAPSHOT],
                 "execute ict_snapshot() ICT")
    except ti_utils.InstallationError:
        failed_icts += 1

    # Mark ZFS root pool "ready" - it was successfully populated and contains
    # valid Solaris instance
    try:
        exec_cmd([ICT_PROG, "ict_mark_root_pool_ready", rootpool_name],
                 "execute ict_mark_root_pool_ready() ICT")
    except ti_utils.InstallationError:
        failed_icts += 1
    
    if failed_icts != 0:
        logging.error("One or more ICTs failed. See previous log messages")
        raise ti_utils.InstallationError
    else:
        logging.info("All ICTs completed successfully")

def perform_ti_install(install_profile, screen, update_status_func, quit_event,
                       time_change_event):
    '''Wrapper to call the do_ti_install() function.
       Sets the variable indicating whether the installation is successful or
       not.

    '''

    try:
        do_ti_install(install_profile, screen, update_status_func, quit_event,
                      time_change_event)
        install_profile.install_succeeded = True
    except ti_utils.InstallationError:
        install_profile.install_succeeded = False
