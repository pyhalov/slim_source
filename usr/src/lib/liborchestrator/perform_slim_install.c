/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <crypt.h>
#include <unistd.h>
#include <time.h>
#include <libgen.h>
#include <netdb.h>
#include <locale.h>
#include <wait.h>

#include "td_lib.h"
#include "cl_database_parms.h"
#include "admldb.h"

#include "orchestrator_private.h"
#include "ict_api.h"
#include <transfermod.h>

#include <ls_api.h>

#define	EMPTY_STR	""
#define	UN(string) ((string) ? (string) : "")

#define	STATE_FILE	"/etc/.sysIDtool.state"

#define	MAXDEVSIZE	100

struct icba {
	om_install_type_t	install_type;
	pid_t			pid;
	om_callback_t		cb;
};

struct transfer_callback {
	char		*target;
	char		*hostname;
	char		*uname;
	char		*lname;
	char		*upasswd;
	char		*rpasswd;
	nvlist_t	**transfer_attr;
	uint_t		transfer_attr_num;
	om_callback_t	cb;
};

struct ti_callback {
	nvlist_t *target_attrs;
};

/*
 * Global Variables
 */
static	boolean_t	install_test = B_FALSE;
static	char		*state_file_path = NULL;
om_install_type_t	install_type;
static	char		*def_locale;
boolean_t		create_swap_and_dump = B_FALSE;
static	pthread_t	ti_thread;
static	int		ti_ret;

/*
 * l_zfs_shared_fs_num is the local representation of ZFS_SHARED_FS_NUM
 * l_zfs_shared_fs_num is initially set to ZFS_SHARED_FS_NUM but
 * if the user does not want a user account the value will be
 * reduced by one.
 */
static	int		l_zfs_shared_fs_num = ZFS_SHARED_FS_NUM;

om_callback_t		om_cb;
char			zfs_device[MAXDEVSIZE];
char			swap_device[MAXDEVSIZE];
char			*zfs_fs_names[ZFS_FS_NUM] = {"/"};
char			zfs_shared_user_login[MAXPATHLEN] = "";
char			*zfs_shared_fs_names[ZFS_SHARED_FS_NUM] =
	{"/export", "/export/home", zfs_shared_user_login};

extern	char		**environ;

struct _shortloclist {
	const char	*shortloc;
	boolean_t	added;
} shortloclist[] = {
	/*
	 * sorting in reverse alphabetical order since
	 * entry for substring (e.g. "zh") needs to come
	 * before longer name (e.g. "zh_TW" or "zh_HK")
	 */
	{ "zh_TW", B_FALSE },
	{ "zh_HK", B_FALSE },
	{ "zh",    B_FALSE },
	{ "sv",    B_FALSE },
	{ "pt_BR", B_FALSE },
	{ "ko",    B_FALSE },
	{ "ja",    B_FALSE },
	{ "it",    B_FALSE },
	{ "fr",    B_FALSE },
	{ "es",    B_FALSE },
	{ "de",    B_FALSE },
	{ NULL,    B_FALSE },
};

image_info_t	image_info = {B_FALSE, 4096, 1.0, "off"};
int		tm_percentage_done = 0;

/*
 * local functions
 */


static void	add_shortloc(const char *locale, FILE *fp);
static char 	*find_state_file();
static void	init_shortloclist(void);
static void	read_and_save_locale(char *path);
static void	remove_component(char *path);
static int	replace_db(char *name, char *value);
static void 	set_system_state(void);
static int	trav_link(char **path);
static void 	write_sysid_state(sys_config *sysconfigp);
static void	notify_error_status(int status);
static void	notify_install_complete();
static int	call_transfer_module(
    nvlist_t		**transfer_attr,
    uint_t		transfer_attr_num,
    char		*target_dir,
    char		*hostname,
    char		*uname,
    char		*lname,
    char		*upasswd,
    char		*rpasswd,
    om_callback_t	cb);
static void	run_install_finish_script(
    char		*target_dir,
    char		*uname,
    char		*lname,
    char		*upasswd,
    char		*rpasswd);
static void	setup_etc_vfstab_for_swap(char *target);
static void	reset_zfs_mount_property(char *target);
static void	activate_be(char *be_name);
static void	transfer_config_files(char *target, int transfer_mode);
static void	handle_TM_callback(const int percent, const char *message);
static int	prepare_zfs_root_pool_attrs(nvlist_t **attrs, char *disk_name);
static int	prepare_zfs_volume_attrs(nvlist_t **attrs,
    uint64_t available_disk_space, boolean_t create_min_swap_only);
static int	prepare_be_attrs(nvlist_t **attrs);
static int	obtain_image_info(image_info_t *info);
static char	*get_dataset_property(char *dataset_name, char *property);
static uint64_t	get_available_disk_space(void);
static uint64_t get_recommended_size_for_software(void);
static uint32_t	get_mem_size(void);
static uint64_t	calc_swap_size(uint64_t available_swap_space);
static uint64_t	calc_dump_size(uint64_t available_dump_space);
static uint64_t	calc_required_swap_size(void);

void 		*do_transfer(void *arg);
void		*do_ti(void *args);

/*
 * om_perform_install
 * This function, called primarily from the GUI Installer or the Automated
 * Installer (AI), will setup configuration and call install/upgrade
 * function(s).
 *
 * Input:	nvlist_t *uchoices - The user choices will be provided as
 *		name-value pairs
 *		void *cb - callback function to inform the Installer about
 *		the progress.
 * Output:	None
 * Return:	OM_SUCCESS, if the install program started succcessfully
 *		OM_FAILURE, if the there is a failure
 * Notes:	The user selected configuration is passed from the Installer
 *		in the form of name-value pairs
 *		The current values passed are:
 *		install_type - uint8_t (initial_install/upgrade)
 *		disk name - String (only for initial_install- example c0d0)
 *		upgrade target - String (only for upgrade - example c0d0s0)
 *		list of locales to be installed - String
 *		default locale - String
 *		user name - String - The name of the user account to be created
 *		user password - String - user password
 *		root password - String - root password
 *
 *              The Installer optionally also specifies the transfer mode
 *              desired (IPS or CPIO) in the form of name-value pairs.
 */

int
om_perform_install(nvlist_t *uchoices, om_callback_t cb)
{
	char		*name;
	char		*lname = NULL, *rpasswd = NULL, *hostname = NULL,
	    *uname = NULL, *upasswd = NULL;
	int		status = OM_SUCCESS;
	nvlist_t	*target_attrs = NULL, **transfer_attr;
	uint_t		transfer_attr_num;
	uint8_t		type;
	char		*ti_test = getenv("TI_SLIM_TEST");
	int		ret = 0;

	if (uchoices == NULL) {
		om_set_error(OM_BAD_INPUT);
	}
	if (!ti_test) {

	/*
	 * Get the install_type
	 */
	if (nvlist_lookup_uint8(uchoices,
	    OM_ATTR_INSTALL_TYPE, &type) != 0) {
		om_set_error(OM_NO_INSTALL_TYPE);
		return (OM_FAILURE);
	}

	/*
	 * Supports only initial_install and upgrade
	 */
	if (type != OM_INITIAL_INSTALL) {
		om_set_error(OM_BAD_INSTALL_TYPE);
		return (OM_FAILURE);
	}
	install_type = type;

	/*
	 * Special value for testing
	 */
	if (nvlist_lookup_boolean_value(uchoices,
	    OM_ATTR_INSTALL_TEST, (boolean_t *)&install_test) != 0) {
		install_test = B_FALSE;
	}
	}

	/*
	 * Now process initial install
	 * Get the disk name - Install target
	 */
	if (nvlist_lookup_string(uchoices, OM_ATTR_DISK_NAME, &name) != 0) {
		om_debug_print(OM_DBGLVL_ERR, "No install target\n");
		om_set_error(OM_NO_INSTALL_TARGET);
		return (OM_FAILURE);
	}

	if (!is_diskname_valid(name)) {
		om_set_error(OM_BAD_INSTALL_TARGET);
		return (OM_FAILURE);
	}

	/*
	 * For initial install, set up the following things.
	 * 1. Timezone
	 * 2. default locale
	 * 3. Root Password
	 * 4. User Name
	 * 5. User password
	 * 6. Host/nodename
	 */

	if (!ti_test) {
	/*
	 * Get the default locale. Save it off for later. We don't
	 * set the system default locale until after the installation
	 * has completed. XXX will Slim have this set from GUI?
	 */
	if (nvlist_lookup_string(uchoices,
	    OM_ATTR_DEFAULT_LOCALE, &def_locale) != 0) {
		/*
		 * Default locale is not passed, so don't set it
		 * Log the information and continue
		 */
		om_debug_print(OM_DBGLVL_WARN, "OM_ATTR_DEFAULT_LOCALE not set,"
		    "default locale is null\n");
		om_log_print("Default locale is NULL\n");
		def_locale = NULL;
	} else {
		om_debug_print(OM_DBGLVL_INFO, "Default locale specified: %s\n",
		    def_locale);
	}
	/*
	 * Get the root password
	 */
	if (nvlist_lookup_string(uchoices,
	    OM_ATTR_ROOT_PASSWORD, &rpasswd) != 0) {
		/*
		 * Root password is not passed, so don't set it
		 * Log the information and set the default password
		 */
		om_debug_print(OM_DBGLVL_WARN, "OM_ATTR_ROOT_PASSWORD not set,"
		    "set the default root password\n");
		om_log_print("Root password not specified, set to default\n");
		rpasswd = OM_DEFAULT_ROOT_PASSWORD;
	} else {
		om_debug_print(OM_DBGLVL_INFO, "Got root passwd\n");
	}

	/*
	 * Get the user name,if set, which is different than the login
	 * name.
	 */

	if (nvlist_lookup_string(uchoices,
	    OM_ATTR_USER_NAME, &uname) != 0) {
		/*
		 * User name is not passed, so don't set it
		 * Log the information and continue
		 */
		om_debug_print(OM_DBGLVL_WARN, "OM_ATTR_USER_NAME not set,"
		    "User name not available\n");
		om_log_print("User name not specified\n");
	}
	if (uname) {
		om_debug_print(OM_DBGLVL_INFO, "User name set to "
		    "%s\n", uname);

	} else {
		uname = EMPTY_STR;
	}

	if (nvlist_lookup_string(uchoices, OM_ATTR_LOGIN_NAME, &lname) != 0) {
		/*
		 * No login name, don't worry about getting passwd info.
		 * Log this data and move on.
		 */
		l_zfs_shared_fs_num = ZFS_SHARED_FS_NUM - 1;
		lname = EMPTY_STR;
		upasswd = OM_DEFAULT_USER_PASSWORD;
		om_debug_print(OM_DBGLVL_WARN,
		    "OM_ATTR_LOGIN_NAME not set,"
		    "User login name not available\n");
		om_log_print("User login name not specified\n");
	} else {
		/*
		 * we got the user name.
		 * Get the password
		 */
		om_debug_print(OM_DBGLVL_INFO, "User login name set to "
		    "%s\n", lname);

		(void) snprintf(zfs_shared_user_login,
		    sizeof (zfs_shared_user_login),
		    "/export/home/%s", lname);

		om_debug_print(OM_DBGLVL_INFO, "zfs shared user login set to "
		    "%s\n", zfs_shared_user_login);

		if (nvlist_lookup_string(uchoices,
		    OM_ATTR_USER_PASSWORD, &upasswd) != 0) {
			/* Password not specified, use default value */
			upasswd = OM_DEFAULT_USER_PASSWORD;
		} else {

			/*
			 * Got user name and password
			 */
			om_debug_print(OM_DBGLVL_INFO, "Got user password\n");
		}
	}

	if (nvlist_lookup_string(uchoices, OM_ATTR_HOST_NAME,
	    &hostname) != 0) {
		/*
		 * User has cleared default host name for some reason.
		 * NWAM will use dhcp so a dhcp address will become
		 * the host/nodename.
		 */
		hostname = EMPTY_STR;
		om_debug_print(OM_DBGLVL_WARN, "OM_ATTR_HOST_NAME "
		    "not set,"
		    "User probably cleared default host name\n");

	} else {
		/*
		 * Hostname will be set in function call_transfer_module
		 * using ICT ict_set_host_node_name
		 */
		om_debug_print(OM_DBGLVL_INFO, "Hostname will be to %s\n",
		    hostname);
	}

	/*
	 * The .sysIDtool.state file needs to be written before the
	 * install completes. The transfer list is processed
	 * before we return from pfinstall, so update the state
	 * here for install.
	 */
	set_system_state();
	/*
	 * Setup install targets. Set the global orchestrator callback
	 * value for use later. Ick.. this is ugly, but for now, until
	 * TI is finalized we need a way to translate the TI->OM
	 * callbacks.
	 *
	 */
	}
	if (cb) {
		om_cb = cb;
	}
	if (nvlist_alloc(&target_attrs, TI_TARGET_NVLIST_TYPE, 0) != 0) {
		om_log_print("Could not create target list.\n");
		return (OM_NO_SPACE);
	}

	/*
	 * Set fdisk configuration attributes
	 */
	if (slim_set_fdisk_attrs(target_attrs, name) != 0) {
		om_log_print("Couldn't set fdisk attributes.\n");
		/*
		 * Will be set in function above.
		 */
		nvlist_free(target_attrs);
		return (om_get_error());
	}
	om_log_print("Set fdisk attrs\n");

	/*
	 * If installer was restarted after the failure, it is necessary
	 * to destroy the pool previously created by the installer.
	 *
	 * If there is a root pool manually imported by the user with
	 * the same name which will be finally picked up by installer
	 * for target root pool, there is nothing we can do at this point.
	 * Log warning message and exit.
	 */

	ret = td_safe_system("/usr/sbin/zpool list " ROOTPOOL_NAME, B_TRUE);
	if ((ret == -1) || WEXITSTATUS(ret) != 0) {
		om_debug_print(OM_DBGLVL_INFO, "Root pool " ROOTPOOL_NAME
		    " doesn't exist\n");
	} else {
		char		*rpool_property;
		nvlist_t	*ti_attrs;
		ti_errno_t	ti_status;

		/*
		 * If ZFS pool was created by the installer and not finalized,
		 * it can be safely removed and installation can proceed.
		 */

		rpool_property = get_dataset_property(ROOT_DATASET_NAME,
		    TI_RPOOL_PROPERTY_STATE);

		om_debug_print(OM_DBGLVL_INFO, "%s %s: %s\n",
		    ROOT_DATASET_NAME, TI_RPOOL_PROPERTY_STATE,
		    rpool_property != NULL ? rpool_property : "not defined");

		/*
		 * If property can't be obtained, it must be assumed that
		 * root pool contains valid Solaris instance, otherwise
		 * installer might destroy older Solaris release which didn't
		 * support that feature.
		 */

		if ((rpool_property == NULL) ||
		    strcmp(rpool_property, TI_RPOOL_BUSY) != 0) {
			om_log_print("Root pool " ROOTPOOL_NAME " exists,"
			    " we can't proceed with the installation\n");

			om_set_error(OM_ZFS_ROOT_POOL_EXISTS);
			return (OM_FAILURE);
		}

		om_log_print("Root pool " ROOTPOOL_NAME " doesn't "
		    "contain valid Solaris instance, it will be "
		    "released\n");

		if (nvlist_alloc(&ti_attrs, TI_TARGET_NVLIST_TYPE, 0) != 0) {
			om_log_print("Could not create target list.\n");
			om_set_error(OM_NO_SPACE);
			return (OM_FAILURE);
		}

		if (prepare_zfs_root_pool_attrs(&ti_attrs, name) !=
		    OM_SUCCESS) {
			om_log_print("Could not prepare ZFS root pool "
			    "attribute set\n");
			nvlist_free(ti_attrs);
			return (OM_FAILURE);
		}

		ti_status = ti_release_target(ti_attrs);
		nvlist_free(ti_attrs);

		if (ti_status != TI_E_SUCCESS) {
			om_log_print("Couldn't release ZFS root pool "
			    ROOTPOOL_NAME "\n");

			om_set_error(OM_ZFS_ROOT_POOL_EXISTS);
			return (OM_FAILURE);
		}

		/*
		 * Finally, clean up target mount point
		 */

		om_log_print("Cleaning up target mount point "
		    INSTALLED_ROOT_DIR "\n");

		(void) td_safe_system("/usr/bin/rm -fr "
		    INSTALLED_ROOT_DIR "/*", B_TRUE);
	}

	/*
	 * Start a thread to call TI module for fdisk & vtoc targets.
	 */

	ti_ret = pthread_create(&ti_thread, NULL, do_ti, target_attrs);
	if (ti_ret != 0) {
		om_set_error(OM_ERROR_THREAD_CREATE);
		return (OM_FAILURE);
	}

	if (nvlist_lookup_nvlist_array(uchoices, OM_ATTR_TRANSFER,
	    &transfer_attr, &transfer_attr_num) != 0) {
		transfer_attr = NULL;
	}

	/*
	 * Start the install.
	 */
	if (call_transfer_module(transfer_attr, transfer_attr_num,
	    INSTALLED_ROOT_DIR, hostname, uname, lname, upasswd,
	    rpasswd, cb) != OM_SUCCESS) {
		om_log_print("Initial install failed\n");
		status = OM_FAILURE;
		om_set_error(OM_INITIAL_INSTALL_FAILED);
		goto install_return;
	}
	om_debug_print(OM_DBGLVL_INFO, "om_perform_install() returned"
	    " success. The install is started\n");
install_return:
	return (status);
}

/*
 * get_mem_size
 * Function obtains information about amount of physical memory
 * installed. If it can't be determined, 0 is returned.
 *
 * Output:	size of physical memory in MiB, 0 if it can't be determined
 */

static uint32_t
get_mem_size(void)
{
	long		pages;
	uint64_t	page_size = PAGESIZE;
	uint32_t	mem_size;

	if ((pages = sysconf(_SC_PHYS_PAGES)) == -1) {
		om_debug_print(OM_DBGLVL_WARN,
		    "Couldn't obtain size of physical memory\n");

		return (0);
	}

	mem_size = (pages * page_size)/ONE_MB_TO_BYTE;

	om_debug_print(OM_DBGLVL_INFO,
	    "Size of physical memory: %lu MiB\n", mem_size);

	return (mem_size);
}


/*
 * calc_swap_size
 *
 * Function calculates size of swap in MiB based on amount of
 * physical memory available.
 *
 * If size of memory can't be determined, minimum swap is returned.
 * If less than calculated space is available, swap size will
 * be adjusted (trimmed down to available disk space)
 *
 * memory   swap
 * -------------
 *  <1G    0,5G
 *  1-64G  0,5-32G (1/2 of memory)
 *  >64G   32G
 *
 * Input:	available_swap_space - disk space which can be used for swap
 *
 * Output:	size of swap in MiB
 */

static uint64_t
calc_swap_size(uint64_t available_swap_space)
{
	uint32_t	mem_size;
	uint64_t	swap_size;

	if ((mem_size = get_mem_size()) == 0) {
		om_debug_print(OM_DBGLVL_WARN,
		    "Couldn't obtain size of physical memory,"
		    "swap size will be set to %dMiB\n", MIN_SWAP_SIZE);

		return (MIN_SWAP_SIZE);
	}

	swap_size = mem_size / 2;

	om_debug_print(OM_DBGLVL_INFO,
	    "Calculated swap size: %llu MiB\n", swap_size);

	/*
	 * If there is less disk space available on target which
	 * can be dedicated to swap device, adjust the swap size
	 * accordingly.
	 */

	om_debug_print(OM_DBGLVL_INFO,
	    "available_swap_space=%llu MiB\n", available_swap_space);

	if (available_swap_space < swap_size)
		swap_size = available_swap_space;

	swap_size = limit_min_max(swap_size, MIN_SWAP_SIZE, MAX_SWAP_SIZE);

	om_debug_print(OM_DBGLVL_INFO,
	    "Adjusted swap size: %llu MiB\n", swap_size);

	return (swap_size);
}

/*
 * calc_required_swap_size
 *
 * Function calculates size of swap in MiB which is required
 * in order to run installer successfully.
 *
 * If there is less than SWAP_MIN_MEMORY_SIZE amount of physical
 * memory available, swap will be mandatory.
 *
 * Output:	size of required swap in MiB
 */

static uint64_t
calc_required_swap_size(void)
{
	static int	required_swap_size = -1;
	uint32_t	mem;

	/* calculate it only once */

	if (required_swap_size != -1)
		return ((uint64_t)required_swap_size);

	if ((mem = get_mem_size()) < SWAP_MIN_MEMORY_SIZE) {
		om_log_print("System reports only %lu MB of physical memory, "
		    "swap will be created\n", mem);

		required_swap_size = MIN_SWAP_SIZE;
	} else {
		om_log_print("System reports enough physical memory "
		    "for installation, swap is optional\n");

		required_swap_size = 0;
	}

	return ((uint64_t)required_swap_size);
}

/*
 * calc_dump_size
 *
 * Function calculates size of dump in MiB based on amount of
 * physical memory available.
 *
 * If size of memory can't be determined, minimum dump is returned.
 * If less than calculated space is available, dump size will
 * be adjusted (trimmed down to available disk space)
 *
 * memory  dump
 * -------------
 *  <0,5G   256M
 *  0,5-32G 256M-16G (1/2 of memory)
 *  >32G    16G
 *
 * Input:	available_dump_space - disk space which can be used for swap
 *
 * Output:	size of dump in MiB
 */

static uint64_t
calc_dump_size(uint64_t available_dump_space)
{
	uint32_t	mem_size;
	uint64_t	dump_size;

	if ((mem_size = get_mem_size()) == 0) {
		om_debug_print(OM_DBGLVL_WARN,
		    "Couldn't obtain size of physical memory,"
		    "dump size will be set to %dMiB\n", MIN_DUMP_SIZE);

		return (MIN_SWAP_SIZE);
	}

	dump_size = mem_size / 2;

	om_debug_print(OM_DBGLVL_INFO,
	    "Calculated dump size: %llu MiB\n", dump_size);

	/*
	 * If there is less disk space available on target which
	 * can be dedicated to dump device, adjust the dump size
	 * accordingly.
	 */

	om_debug_print(OM_DBGLVL_INFO,
	    "available_dump_space=%llu MiB\n", available_dump_space);

	if (available_dump_space < dump_size)
		dump_size = available_dump_space;

	dump_size = limit_min_max(dump_size, MIN_DUMP_SIZE, MAX_DUMP_SIZE);

	om_debug_print(OM_DBGLVL_INFO,
	    "Adjusted dump size: %llu MiB\n", dump_size);

	return (dump_size);
}


/*
 * call_transfer_module
 * This function creates the a thread to call the transfer module
 * Input:	transfer_attr - list of attrs that describe the transfer
 * 		target_dir - The mounted directory for alternate root
 *		hostname - The user specified host name.
 *		uname - The user specified user name, gcos.
 *		lname - The user specified login name.
 *		upasswd - The user specified user password
 *		rpasswd - The user specified root password
 *		om_call_back_t *cb - The callback function
 * Output:	None
 * Return:	OM_SUCCESS, if the all threads are started successfully
 *		OM_FAILURE, if the there is a failure
 */
static int
call_transfer_module(
    nvlist_t		**transfer_attr,
    uint_t		transfer_attr_num,
    char		*target_dir,
    char		*hostname,
    char		*uname,
    char		*lname,
    char		*upasswd,
    char		*rpasswd,
    om_callback_t	cb)
{
	struct transfer_callback	*tcb_args;
	int				i, ret;
	pthread_t			transfer_thread;

	if (target_dir == NULL) {
		om_set_error(OM_NO_INSTALL_TARGET);
		return (OM_FAILURE);
	}

	if (hostname == NULL) {
		om_set_error(OM_NO_HOSTNAME);
		return (OM_FAILURE);
	}

	if (uname == NULL) {
		om_set_error(OM_NO_UNAME);
		return (OM_FAILURE);
	}

	if (lname == NULL) {
		om_set_error(OM_NO_LNAME);
		return (OM_FAILURE);
	}

	if (upasswd == NULL) {
		om_set_error(OM_NO_UPASSWD);
		return (OM_FAILURE);
	}

	if (rpasswd == NULL) {
		om_set_error(OM_NO_RPASSWD);
		return (OM_FAILURE);
	}

	/*
	 * Populate the callback arguments structure, tcb_args.
	 */
	tcb_args = (struct transfer_callback *)
	    calloc(1, sizeof (struct transfer_callback));
	if (tcb_args == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * Put target in callback arguments structure, tcb_args.
	 */
	tcb_args->target = strdup(target_dir);
	if (tcb_args->target == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * Put hostname in callback arguments structure, tcb_args.
	 */
	tcb_args->hostname = strdup(hostname);
	if (tcb_args->hostname == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * Put user name (uname) in callback arguments structure, tcb_args.
	 */
	tcb_args->uname = strdup(uname);
	if (tcb_args->uname == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * Put login name (lname) in callback arguments structure, tcb_args.
	 */
	tcb_args->lname = strdup(lname);
	if (tcb_args->lname == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * Put user passwd (upasswd) in callback arguments structure, tcb_args.
	 */
	tcb_args->upasswd = strdup(upasswd);
	if (tcb_args->upasswd == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * Put root passwd (rpasswd) in callback arguments structure, tcb_args.
	 */
	tcb_args->rpasswd = strdup(rpasswd);
	if (tcb_args->rpasswd == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}

	/*
	 * transfer_attr describe the transfer in question.
	 * The only time transfer attrs are set is during
	 * an automated install. In all other cases, they
	 * are set to NULL
	 */
	if (transfer_attr != NULL) {
		tcb_args->transfer_attr =
		    malloc(sizeof (nvlist_t *) * transfer_attr_num);

		if (tcb_args->transfer_attr == NULL) {
			om_set_error(OM_NO_SPACE);
			return (OM_FAILURE);
		}

		if (transfer_attr_num != 2) {
			om_set_error(OM_NO_SPACE);
			return (OM_FAILURE);
		}

		for (i = 0; i < transfer_attr_num; i++) {
			if (nvlist_dup(transfer_attr[i],
			    &(tcb_args->transfer_attr[i]), 0) != 0) {
				om_set_error(OM_NO_SPACE);
				return (OM_FAILURE);
			}
		}
	}

	/*
	 * Create a thread for running Transfer Module
	 */
	ret = pthread_create(&transfer_thread, NULL,
	    do_transfer, (void *)tcb_args);

	if (ret != 0) {
		om_set_error(OM_ERROR_THREAD_CREATE);
		return (OM_FAILURE);
	}

	return (OM_SUCCESS);
}

void *
do_ti(void *args)
{
	struct ti_callback	*ti_args;
	static int		status = 0;
	ti_errno_t		ti_status;
	nvlist_t		*attrs = (nvlist_t *)args;
	om_callback_info_t	cb_data;
	uintptr_t		app_data = 0;
	char			*disk_name;
	nvlist_t		*ti_ex_attrs;
	uint64_t		available_disk_space;
	uint64_t		recommended_size;

	ti_args = (struct ti_callback *)
	    calloc(1, sizeof (struct ti_callback));

	if (ti_args == NULL) {
		om_log_print("Couldn't create ti_callback args\n");
		om_set_error(OM_NO_SPACE);
		status = -1;
		goto ti_error;
	}

	nvlist_dup(attrs, &ti_args->target_attrs, 0);

	if (ti_args->target_attrs == NULL) {
		om_log_print("ti_args == NULL\n");
		om_set_error(OM_NO_TARGET_ATTRS);
		status = -1;
		goto ti_error;
	}

	/*
	 * create fdisk target
	 */

	/* Obtain disk name first */

	if (nvlist_lookup_string(ti_args->target_attrs, TI_ATTR_FDISK_DISK_NAME,
	    &disk_name) != 0) {
		om_debug_print(OM_DBGLVL_ERR, "Disk name not provided, can't "
		    "proceed with target instantiation\n");
		om_set_error(OM_NO_INSTALL_TARGET);
		status = -1;
		goto ti_error;
	}

	/* initialize progress report structures */

	cb_data.num_milestones = 3;
	cb_data.callback_type = OM_INSTALL_TYPE;
	cb_data.curr_milestone = OM_TARGET_INSTANTIATION;
	cb_data.percentage_done = 0;

	/*
	 * create fdisk target
	 */

	ti_status = ti_create_target(ti_args->target_attrs, NULL);

	if (ti_status != TI_E_SUCCESS) {
		om_log_print("Could not create fdisk target\n");
		om_set_error(OM_TARGET_INSTANTIATION_FAILED);
		status = -1;
		goto ti_error;
	}

	cb_data.percentage_done = 20;
	om_cb(&cb_data, app_data);

	/*
	 * create VTOC target
	 */

	if (nvlist_alloc(&ti_ex_attrs, TI_TARGET_NVLIST_TYPE, 0) != 0) {
		om_log_print("Could not create target list.\n");
		om_set_error(OM_NO_SPACE);
		status = -1;
		goto ti_error;
	}

	if (slim_set_slice_attrs(ti_ex_attrs, disk_name) != 0) {
		om_log_print("Couldn't set slice attributes. \n");
		nvlist_free(ti_ex_attrs);
		status = -1;
		goto ti_error;
	}

	ti_status = ti_create_target(ti_ex_attrs, NULL);
	nvlist_free(ti_ex_attrs);

	if (ti_status != TI_E_SUCCESS) {
		om_log_print("Could not create VTOC target\n");
		om_set_error(OM_CANT_CREATE_VTOC_TARGET);
		status = -1;
		goto ti_error;
	}

	cb_data.percentage_done = 40;
	om_cb(&cb_data, app_data);

	/*
	 * Create ZFS root pool.
	 */

	om_log_print("Set zfs root pool device\n");

	if (prepare_zfs_root_pool_attrs(&ti_ex_attrs, disk_name) !=
	    OM_SUCCESS) {
		om_log_print("Could not prepare ZFS root pool attribute set\n");
		nvlist_free(ti_ex_attrs);
		status = -1;
		goto ti_error;
	}

	om_log_print("creating zpool\n");

	/* call TI for creating zpool */

	ti_status = ti_create_target(ti_ex_attrs, NULL);

	nvlist_free(ti_ex_attrs);

	if (ti_status != TI_E_SUCCESS) {
		om_log_print("Could not create ZFS root pool target\n");
		om_set_error(OM_CANT_CREATE_ZPOOL);
		status = -1;
		goto ti_error;
	}

	cb_data.percentage_done = 60;
	om_cb(&cb_data, app_data);

	/*
	 * Create swap & dump on ZFS volumes
	 */

	available_disk_space = get_available_disk_space();

	/*
	 * save swap device name for later purposes
	 */

	snprintf(swap_device, sizeof (swap_device), "/dev/zvol/dsk/"
	    ROOTPOOL_NAME "/" TI_ZFS_VOL_NAME_SWAP);

	/*
	 * Calculate actual disk space, which can be utilized for
	 * swap and dump. If zero, only minimum swap and dump
	 * will be created
	 */

	recommended_size = get_recommended_size_for_software();

	if (available_disk_space < recommended_size) {
		available_disk_space = 0;
	} else {
		available_disk_space -= recommended_size;
	}

	if (create_swap_and_dump) {
		om_log_print("Creating swap and dump on ZFS volumes\n");

		if (prepare_zfs_volume_attrs(&ti_ex_attrs, available_disk_space,
		    B_FALSE) != OM_SUCCESS) {
			om_log_print("Could not prepare ZFS volume attribute "
			    "set\n");

			nvlist_free(ti_ex_attrs);
			status = -1;
			goto ti_error;
		}

		/* call TI for creating ZFS volumes */

		ti_status = ti_create_target(ti_ex_attrs, NULL);

		nvlist_free(ti_ex_attrs);

		if (ti_status != TI_E_SUCCESS) {
			om_log_print("Could not create ZFS volume target\n");
			om_set_error(OM_CANT_CREATE_ZVOL);
			status = -1;
			goto ti_error;
		}
	} else if (calc_required_swap_size() != 0) {
		/*
		 * Swap will be created on a ZFS volume if insufficient
		 * amount of physical memory is available.
		 */

		om_log_print("There is not enough physical memory available, "
		    "the installer will create ZFS volume for swap\n");

		if (prepare_zfs_volume_attrs(&ti_ex_attrs, available_disk_space,
		    B_TRUE) != OM_SUCCESS) {
			om_log_print("Could not prepare ZFS volume attribute "
			    "set\n");

			nvlist_free(ti_ex_attrs);
			status = -1;
			goto ti_error;
		}

		om_log_print("creating ZFS volume for swap\n");

		/* call TI for creating ZFS volumes */

		ti_status = ti_create_target(ti_ex_attrs, NULL);

		nvlist_free(ti_ex_attrs);

		if (ti_status != TI_E_SUCCESS) {
			om_log_print("Could not create ZFS volume target\n");
			om_set_error(OM_CANT_CREATE_ZVOL);
			status = -1;
			goto ti_error;
		}
	} else {
		om_log_print("There is not enough disk space available for "
		    "swap and dump, they won't be created\n");

		om_log_print("%lluGiB of free space is required "
		    "for swap and dump devices, please refer to recommended "
		    "value on Disk screen\n",
		    (om_get_recommended_size(NULL, NULL) + ONE_GB_TO_MB / 2) /
		    ONE_GB_TO_MB);

		swap_device[0] = '\0';
	}

	cb_data.percentage_done = 80;
	om_cb(&cb_data, app_data);

	/*
	 * Create BE
	 */

	if (prepare_be_attrs(&ti_ex_attrs) != OM_SUCCESS) {
		om_log_print("Could not prepare BE attribute set\n");
		nvlist_free(ti_ex_attrs);
		status = -1;
		goto ti_error;
	}

	ti_status = ti_create_target(ti_ex_attrs, NULL);

	nvlist_free(ti_ex_attrs);

	if (ti_status != TI_E_SUCCESS) {
		om_log_print("Could not create BE target\n");
		status = -1;
		goto ti_error;
	}

	cb_data.percentage_done = 99;
	om_cb(&cb_data, app_data);

ti_error:

	cb_data.num_milestones = 3;
	cb_data.callback_type = OM_INSTALL_TYPE;

	if (status != 0) {
		om_log_print("TI process failed\n");
		cb_data.curr_milestone = OM_INVALID_MILESTONE;
		cb_data.percentage_done = OM_TARGET_INSTANTIATION_FAILED;
	} else {
		om_log_print("TI process completed successfully \n");
		cb_data.curr_milestone = OM_TARGET_INSTANTIATION;
		cb_data.percentage_done = 100;
	}

	om_cb(&cb_data, app_data);
	pthread_exit((void *)&status);
	/* LINTED [no return statement] */
}

/*
 * do_transfer
 * This function calls the api to do the actual transfer of install contents
 * from cd/dvd/ips to hard disk
 * Input:	void *arg - Pointer to the parameters needed to call
 *		transfer mdoule. Currently the full path of the alternate root
 *		and callback parameter
 * Output:	None
 * Return:	status is returned as part of pthread_exit function
 */
void *
do_transfer(void *args)
{
	struct transfer_callback	*tcb_args;
	nvlist_t			**transfer_attr;
	uint_t				transfer_attr_num;
	int				i, status;
	int				transfer_mode = OM_CPIO_TRANSFER;
	char				cmd[MAXPATHLEN];
	char				*rpool_id;
	int				ret, value;
	void				*exit_val;

	(void) pthread_join(ti_thread, &exit_val);

	ti_ret += *(int *)exit_val;
	if (ti_ret != 0) {
		om_log_print("Target instantiation failed exit_val=%d\n",
		    ti_ret);
		om_set_error(OM_TARGET_INSTANTIATION_FAILED);
		notify_error_status(OM_TARGET_INSTANTIATION_FAILED);
		status = -1;
		pthread_exit((void *)&status);
	}

	om_log_print("Transfer process initiated\n");

	tcb_args = (struct transfer_callback *)args;
	transfer_attr = tcb_args->transfer_attr;
	transfer_attr_num = tcb_args->transfer_attr_num;

	if (tcb_args->target == NULL) {
		if (transfer_attr != NULL) {
			for (i = 0; i < transfer_attr_num; i++)
				nvlist_free(transfer_attr[i]);
			free(transfer_attr);
		}
		om_set_error(OM_NO_TARGET_ATTRS);
		notify_error_status(OM_NO_TARGET_ATTRS);
		status = -1;
		pthread_exit((void *)&status);
	}

	/*
	 * Determine the mode of operation (IPS or CPIO) and
	 * set up the transfer appropriately
	 *
	 * If the mode is not specified, CPIO is assumed as the default
	 */
	if (transfer_attr != NULL) {
		if (nvlist_lookup_uint32(transfer_attr[0], TM_ATTR_MECHANISM,
		    ((uint32_t *)&value)) != 0) {
			for (i = 0; i < transfer_attr_num; i++)
				nvlist_free(transfer_attr[i]);
			free(transfer_attr);
			om_set_error(OM_NO_TARGET_ATTRS);
			notify_error_status(OM_NO_TARGET_ATTRS);
			status = -1;
			pthread_exit((void *)&status);
		}
		if (value == TM_PERFORM_IPS)
			transfer_mode = OM_IPS_TRANSFER;
	} else {
		transfer_attr_num = 1;
		transfer_attr = malloc(sizeof (nvlist_t *) * transfer_attr_num);
		if (nvlist_alloc(transfer_attr, NV_UNIQUE_NAME, 0) != 0) {
			om_set_error(OM_NO_SPACE);
			notify_error_status(OM_NO_SPACE);
			status = -1;
			pthread_exit((void *)&status);
		}

		if (nvlist_add_uint32(*transfer_attr, TM_ATTR_MECHANISM,
		    TM_PERFORM_CPIO) != 0) {
			for (i = 0; i < transfer_attr_num; i++)
				nvlist_free(transfer_attr[i]);
			free(transfer_attr);

			om_set_error(OM_NO_SPACE);
			notify_error_status(OM_NO_SPACE);
			status = -1;
			pthread_exit((void *)&status);
		}

		if (nvlist_add_uint32(*transfer_attr, TM_CPIO_ACTION,
		    TM_CPIO_ENTIRE) != 0) {
			for (i = 0; i < transfer_attr_num; i++)
				nvlist_free(transfer_attr[i]);
			free(transfer_attr);

			om_set_error(OM_NO_SPACE);
			notify_error_status(OM_NO_SPACE);
			status = -1;
			pthread_exit((void *)&status);
		}

		if (nvlist_add_string(*transfer_attr, TM_CPIO_SRC_MNTPT,
		    "/") != 0) {
			for (i = 0; i < transfer_attr_num; i++)
				nvlist_free(transfer_attr[i]);
			free(transfer_attr);

			om_set_error(OM_NO_SPACE);
			notify_error_status(OM_NO_SPACE);
			status = -1;
			pthread_exit((void *)&status);
		}

		if (nvlist_add_string(*transfer_attr, TM_CPIO_DST_MNTPT,
		    tcb_args->target) != 0) {
			for (i = 0; i < transfer_attr_num; i++)
				nvlist_free(transfer_attr[i]);
			free(transfer_attr);

			om_set_error(OM_NO_SPACE);
			notify_error_status(OM_NO_SPACE);
			status = -1;
			pthread_exit((void *)&status);
		}
	}

	if (transfer_mode == OM_IPS_TRANSFER)
		status = TM_perform_transfer_ips(transfer_attr,
		    handle_TM_callback);
	else
		status = TM_perform_transfer_cpio(*transfer_attr,
		    handle_TM_callback);

	if (status == TM_SUCCESS) {

		/*
		 * Set the language locale.
		 */
		if (def_locale != NULL) {
			if (ict_set_lang_locale(tcb_args->target,
			    def_locale, transfer_mode) != ICT_SUCCESS) {
				om_log_print("Failed to set locale: "
				    "%s\n%s\n", def_locale,
				    ICT_STR_ERROR(ict_errno));
			}
		}

		/*
		 * Create user directory if needed
		 */

		if (ict_configure_user_directory(INSTALLED_ROOT_DIR,
		    tcb_args->lname) != ICT_SUCCESS) {
			om_log_print("Couldn't configure user directory\n"
			    "for user: %s\n%s\n", tcb_args->lname,
			    ICT_STR_ERROR(ict_errno));
		}

		/*
		 * If swap was created, add appropriate entry to
		 * <target>/etc/vfstab
		 */

		if (swap_device[0] != '\0') {
			setup_etc_vfstab_for_swap(tcb_args->target);
		}

		if (ict_set_host_node_name(tcb_args->target,
		    tcb_args->hostname, transfer_mode) != ICT_SUCCESS) {
			om_log_print("Couldn't set the host and node name\n"
			    "to hostname: %s\n%s\n", tcb_args->hostname,
			    ICT_STR_ERROR(ict_errno));
		}

		if (ict_set_user_profile(tcb_args->target, tcb_args->lname) !=
		    ICT_SUCCESS) {
			om_log_print("Couldn't set the user environment\n"
			    "for user: %s\n%s\n",
			    tcb_args->lname, ICT_STR_ERROR(ict_errno));
		}

		activate_be(INIT_BE_NAME);

		if (ict_installgrub(tcb_args->target, zfs_device) !=
		    ICT_SUCCESS) {
			om_log_print("installgrub failed\n%s\n",
			    ICT_STR_ERROR(ict_errno));
		}

		if (ict_set_user_role(tcb_args->target, tcb_args->lname) !=
		    ICT_SUCCESS) {
			om_log_print("Couldn't set the user role\n"
			    "for user: %s\n%s\n", tcb_args->lname,
			    ICT_STR_ERROR(ict_errno));
		}

		run_install_finish_script(tcb_args->target,
		    tcb_args->uname, tcb_args->lname,
		    tcb_args->upasswd, tcb_args->rpasswd);

		/*
		 * Take a snapshot of the installation.
		 */
		if (ict_snapshot(INIT_BE_NAME, INSTALL_SNAPSHOT) !=
		    ICT_SUCCESS) {
			om_log_print("Failed to generate snapshot\n"
			    "pool: %s\nsnapshot: %s\n%s\n",
			    INIT_BE_NAME, INSTALL_SNAPSHOT,
			    ICT_STR_ERROR(ict_errno));
		}

		/*
		 * mark ZFS root pool 'ready' - it was successfully populated
		 * and contains valid Solaris instance
		 */

		om_log_print("Marking root pool as 'ready'\n");

		if (ict_mark_root_pool_ready(ROOTPOOL_NAME) != ICT_SUCCESS)
			om_log_print("%s\n", ICT_STR_ERROR(ict_errno));
		else
			om_debug_print(OM_DBGLVL_INFO,
			    "Root pool %s was marked as 'ready'\n",
			    ROOTPOOL_NAME);

		reset_zfs_mount_property(tcb_args->target);

		/*
		 * Notify the caller that install is completed
		 */

		notify_install_complete();
	} else {
		om_debug_print(OM_DBGLVL_WARN, NSI_TRANSFER_FAILED, status);
		om_log_print(NSI_TRANSFER_FAILED, status);
		notify_error_status(OM_TRANSFER_FAILED);
	}

	for (i = 0; i < transfer_attr_num; i++)
		nvlist_free(transfer_attr[i]);
	free(transfer_attr);

	pthread_exit((void *)&status);
	/* LINTED [no return statement] */
}

/*
 * handle_TM_callback
 * This function handles the callbacks for TM
 * It builds the callback data the GUI expects
 * Input:	percent - percentage complete
 *		message - localized text message for GUI to display
 * Output:	None
 * Return:	None
 */
static void
handle_TM_callback(const int percent, const char *message)
{
	om_callback_info_t cb_data;

	cb_data.num_milestones = 3;
	cb_data.curr_milestone = OM_SOFTWARE_UPDATE;
	cb_data.callback_type = OM_INSTALL_TYPE;
	cb_data.percentage_done = percent;
	cb_data.message = message;
	om_cb(&cb_data, 0);
	tm_percentage_done = percent;
}


/*
 * Parsing function to get the percentage value from the string.
 * The string will be like "percent=11"
 * The output is an integer from 0 - 100
 */
int16_t
get_the_percentage(char *str)
{
	char	*ptr, *ptr1;
	int16_t	percent;

	/*
	 * Look for percent="N"
	 */
	ptr = strstr(str, "percent=");
	if (ptr == NULL) {
		return (-1);
	}
	/*
	 * Find where the number is starting
	 */
	while (isdigit(*ptr) == 0) {
		ptr++;
	}
	ptr1 = strchr(ptr, '"');
	if (ptr1 == NULL) {
		return (-1);
	}
	*ptr1 = '\0';

	errno = 0;
	percent = (int16_t)strtol(ptr, (char **)NULL, 10);
	if (errno != 0) {
		/*
		 * Log the information
		 */
		return (-1);
	}
	return (percent);
}

/*
 * Parsing function to get the current milestone
 * and converts it to integer.
 * The string looks like "<progressStatus"
 */
int
get_the_milestone(char *str)
{
	if (strncasecmp(str, PROGRESS_STATUS,
	    strlen(PROGRESS_STATUS)) == 0) {
		return (OM_SOFTWARE_UPDATE);
	}
	if (strncasecmp(str, TARGET_INSTANTIATION_STATUS,
	    strlen(TARGET_INSTANTIATION_STATUS)) == 0) {
		return (OM_TARGET_INSTANTIATION);
	}
	if (strncasecmp(str, POST_INSTALL_STATUS,
	    strlen(POST_INSTALL_STATUS)) == 0) {
		return (OM_POSTINSTAL_TASKS);
	}
	if (strncasecmp(str, UPGRADE_SPACE_CHECK,
	    strlen(UPGRADE_SPACE_CHECK)) == 0) {
		return (OM_UPGRADE_CHECK);
	}
	if (strncasecmp(str, INSTALLER_FAILED,
	    strlen(INSTALLER_FAILED)) == 0) {
		return (OM_INSTALLER_FAILED);
	}
	return (OM_INVALID_MILESTONE);
}

/*ARGSUSED*/
uint64_t
om_get_min_size(char *media, char *distro)
{
	/*
	 * Size in MB that is the minimum device size we will allow
	 * for installing Slim.
	 *
	 * Get uncompressed size of image and add 20% reserve.
	 *
	 * If system has not enough physical memory for installation,
	 * swap is required and minimum size will account for it.
	 *
	 * Otherwise, create swap and dump only if user dedicated
	 * at least recommended disk size for installation.
	 *
	 * Dump is always optional.
	 *
	 * If information about image size is not available,
	 * default is used (4GiB).
	 */

	if (obtain_image_info(&image_info) != OM_SUCCESS)
		om_log_print("Couldn't read image info file");

	return ((uint64_t)(image_info.image_size *
	    image_info.compress_ratio * 1.2) + calc_required_swap_size());
}


/*ARGSUSED*/
uint64_t
om_get_recommended_size(char *media, char *distro)
{
	/*
	 * Size in MB that is the recommended device size we will allow
	 * for installing Slim.
	 *
	 * Account for one full upgrade, minimal swap and dump volumes
	 * and add nother 2 GiB for additional software.
	 */

	return (get_recommended_size_for_software()
	    + MIN_DUMP_SIZE + MIN_SWAP_SIZE);
}

/*
 * Return maximum usable disk size in MiB
 */
uint32_t
om_get_max_usable_disk_size(void)
{
	return (MAX_USABLE_DISK);
}

/*
 * Return the UID which will be assigned to the new user
 * created by the installer.
 */
uid_t
om_get_user_uid(void)
{
	return ((uid_t)ICT_USER_UID);
}

char *
om_encrypt_passwd(char *passwd, char *username)
{
	char	*e_pw = NULL;
	char	*saltc;
	struct 	passwd	*u_pw = NULL;

	u_pw = getpwnam(username);
	if (u_pw == NULL) {
		u_pw = malloc(sizeof (struct passwd));
		if (u_pw == NULL) {
			om_set_error(OM_NO_SPACE);
			return (NULL);
		}
		u_pw->pw_name = strdup(username);
		if (u_pw->pw_name == NULL) {
			free((void *)u_pw);
			om_set_error(OM_NO_SPACE);
			return (NULL);
		}
	}
	saltc = crypt_gensalt(NULL, u_pw);
	if (saltc == NULL) {
		free((void *)u_pw->pw_name);
		free((void *)u_pw);
		om_set_error(errno);
		return (NULL);
	}

	e_pw = crypt((const char *)passwd, saltc);
	return (e_pw);
}

static void
set_system_state(void)
{
	sys_config	sysconfig;

	sysconfig.configured = 1;
	sysconfig.bootparamed = 1;
	sysconfig.networked = 1;
	sysconfig.extnetwork = 1;
	sysconfig.autobound = 1;
	sysconfig.subnetted = 1;
	sysconfig.passwdset = 1;
	sysconfig.localeset = 1;
	sysconfig.security = 1;
	sysconfig.nfs4domain = 1;
	(void) sprintf(sysconfig.termtype, "sun");

	write_sysid_state(&sysconfig);

}

static int
replace_db(char *name, char *value)
{

	FILE 	*ifp, *ofp;	/* Input & output files */
	int	tmp;
	char	*tmpdir;	/* Temp file name and location */
	char 	*tdb;

	/*
	 * Generate temporary file name to use.  We make sure it's in the same
	 * directory as the db we're processing so that we can use rename to
	 * do the replace later.  Otherwise we run the risk of being on the
	 * wrong filesystem and having rename() fail for that reason.
	 */
	if (name == NULL || value == NULL) {
		om_debug_print(OM_DBGLVL_INFO,
		    "Invalid values for replacing db\n");
		return (OM_FAILURE);
	}
	tdb = strdup(name);
	if (tdb == NULL) {
		om_set_error(OM_NO_SPACE);
		om_log_print("Could not allocate space for %s\n", name);
		return (OM_FAILURE);
	}
	if (trav_link(&tdb) == -1) {
		om_set_error(OM_NO_SUCH_DB_FILE);
		om_log_print("Couldn't fine db file %s\n", name);
		return (OM_FAILURE);
	}

	tmpdir = (char *)malloc(strlen(tdb) + 7);
	if (tmpdir == NULL) {
		om_set_error(OM_NO_SPACE);
		return (OM_FAILURE);
	}
	(void) memset(tmpdir, 0, strlen(tdb) + 7);

	(void) snprintf(tmpdir, strlen(tdb), "%s", tdb);
	(void) strcat(tmpdir, "XXXXXX");
	if ((tmp = mkstemp(tmpdir)) == -1) {
		om_debug_print(OM_DBGLVL_ERR,
		    "Can't create temp file for replacing db\n");
		om_set_error(OM_CANT_CREATE_TMP_FILE);
		free(tmpdir);
		return (OM_FAILURE);
	}

	ofp = fdopen(tmp, "w");
	if (ofp == NULL) {
		om_set_error(OM_CANT_CREATE_TMP_FILE);
		return (OM_FAILURE);
	}

	if (fprintf(ofp, "%s\n", value) == EOF) {
		om_set_error(OM_CANT_WRITE_TMP_FILE);
		(void) fclose(ofp);
		return (OM_FAILURE);
	}

	/* Quick check to make sure we have read & write rights to the file */
	if ((ifp = fopen(tdb, "w")) != NULL)
		(void) fclose(ifp);
	else if (errno != ENOENT) {
		om_debug_print(OM_DBGLVL_ERR,
		    "Cannot open file to rename to\n");
		return (OM_FAILURE);
	}
	(void) fclose(ofp);

	if (rename(tmpdir, tdb) != 0) {
		free(tmpdir);
		om_set_error(OM_SETNODE_FAILURE);
		om_debug_print(OM_DBGLVL_ERR,
		    "Could not rename file %s to %s\n", tmp, name);
		return (OM_FAILURE);
	}
	return (OM_SUCCESS);
}

static char *
find_state_file()
{
	char *path;

	if (state_file_path == NULL) {
		path = STATE_FILE;
		if (trav_link(&path) == 0) {
			state_file_path = strdup(path);
			if (state_file_path == NULL) {
				om_set_error(OM_NO_SPACE);
				return (NULL);
			}
			om_debug_print(OM_DBGLVL_INFO,
			    "State file changing = %s\n", state_file_path);
		} else {
			state_file_path = STATE_FILE;
			om_debug_print(OM_DBGLVL_INFO,
			    "State file changing = %s\n", state_file_path);
		}
	}

	om_debug_print(OM_DBGLVL_INFO,
	    "sydIDtool.state file is %s\n", state_file_path);
	return (state_file_path);
}

static int
trav_link(char **path)
{
	static char	newpath[MAXPATHLEN];
	char 		lastpath[MAXPATHLEN];
	int 		len;
	char 		*tp;

	(void) strcpy(lastpath, *path);
	while ((len = readlink(*path, newpath, sizeof (newpath))) != -1) {
		newpath[len] = '\0';
		if (newpath[0] != '/') {
			tp = strdup(newpath);
			if (tp == NULL) {
				om_set_error(OM_NO_SPACE);
				om_debug_print(OM_DBGLVL_ERR,
				    "Could not allocate space for "
				    "%s\n", newpath);
				return (OM_FAILURE);
			}
			remove_component(lastpath);
			(void) snprintf(newpath, sizeof (newpath), "%s/%s",
			    lastpath, tp);
			free(tp);
		}
		(void) strcpy(lastpath, newpath);
		*path = newpath;
	}
	/* XXX why is this so? XXX */
	if (errno == ENOENT || errno == EINVAL)
		return (OM_SUCCESS);
	return (OM_FAILURE);
}

static void
remove_component(char *path)
{
	char	*p;

	p = strrchr(path, '/');	/* find last '/' */
	if (p == NULL) {
		*path = '\0';
	} else {
		*p = '\0';
	}
}

static void
write_sysid_state(sys_config *sysconfigp)
{
	mode_t	cmask;	/* Current umask */
	FILE	*fp;
	char	*file = NULL;

	cmask = umask((mode_t)022);
	file = find_state_file();
	if (file == NULL) {
		om_set_error(OM_CANT_OPEN_FILE);
		om_debug_print(OM_DBGLVL_WARN,
		    "Could not find sysidtool.state file\n");
		return;
	}

	fp = fopen(file, "w");
	(void) umask(cmask);

	if (fp == NULL) {
		om_debug_print(OM_DBGLVL_WARN,
		    "sysIDtool %s couldn't open: "
		    "errno = %d\n", find_state_file(), errno);
		return;
	}
	/*
	 * Write each state component.
	 */
	(void) fprintf(fp, "%d\t%s\n", sysconfigp->configured,
	    "# System previously configured?");
	om_debug_print(OM_DBGLVL_INFO, "write ( configured): %d\n",
	    sysconfigp->configured);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->bootparamed,
	    "# Bootparams succeeded?");
	om_debug_print(OM_DBGLVL_INFO, "write (bootparamed): %d\n",
	    sysconfigp->bootparamed);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->networked,
	    "# System is on a network?");
	om_debug_print(OM_DBGLVL_INFO, "write (  networked): %d\n",
	    sysconfigp->networked);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->extnetwork,
	    "# Extended network information gathered?");
	om_debug_print(OM_DBGLVL_INFO, "write (ext network): %d\n",
	    sysconfigp->extnetwork);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->autobound,
	    "# Autobinder succeeded?");
	om_debug_print(OM_DBGLVL_INFO, "write (  autobound): %d\n",
	    sysconfigp->autobound);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->subnetted,
	    "# Network has subnets?");
	om_debug_print(OM_DBGLVL_INFO, "write (  subnetted): %d\n",
	    sysconfigp->subnetted);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->passwdset,
	    "# root password prompted for?");
	om_debug_print(OM_DBGLVL_INFO, "write (     passwd): %d\n",
	    sysconfigp->passwdset);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->localeset,
	    "# locale and term prompted for?");
	om_debug_print(OM_DBGLVL_INFO, "write (     locale): %d\n",
	    sysconfigp->localeset);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->security,
	    "# security policy in place");
	om_debug_print(OM_DBGLVL_INFO, "write (   security): %d\n",
	    sysconfigp->security);

	(void) fprintf(fp, "%d\t%s\n", sysconfigp->nfs4domain,
	    "# NFSv4 domain configured");
	om_debug_print(OM_DBGLVL_INFO, "write ( nfs4domain): %d\n",
	    sysconfigp->nfs4domain);
	/*
	 * N.B.: termtype MUST be the last entry in sysIDtool.state,
	 * as suninstall.sh tails this file to get the TERM env variable.
	 */
	(void) fprintf(fp, "%s\n", sysconfigp->termtype);
	om_debug_print(OM_DBGLVL_INFO, "write (       term): %s\n",
	    sysconfigp->termtype);

	(void) fclose(fp);
}

static void
add_shortloc(const char *locale, FILE *fp)
{
	struct _shortloclist    *p = NULL;

	for (p = shortloclist; p->shortloc != NULL; p++) {
		if (strncmp(p->shortloc, locale, strlen(p->shortloc)) == 0) {
			if (p->added == B_FALSE) {
				(void) fprintf(fp, "locale %s\n", p->shortloc);
				p->added = B_TRUE;
			}
			break;
		}
	}
}

static void
init_shortloclist(void)
{
	struct _shortloclist    *p = NULL;
	for (p = shortloclist; p->shortloc != NULL; p++) {
		p->added = B_FALSE;
	}
}

/*
 * Inform GUI of error condition through callback
 */
static	void
notify_error_status(int status)
{
	om_callback_info_t cb_data;

	cb_data.num_milestones = 3;
	cb_data.curr_milestone = -1; /* signals error to GUI */
	cb_data.callback_type = OM_INSTALL_TYPE;
	cb_data.percentage_done = status; /* overload value on error */
	cb_data.message = NULL;
	om_cb(&cb_data, 0);
}

/*
 * Notify the GUI that the installation is complete
 */
static	void
notify_install_complete()
{
	om_callback_info_t cb_data;

	cb_data.num_milestones = 3;
	cb_data.curr_milestone = OM_POSTINSTAL_TASKS;
	cb_data.callback_type = OM_INSTALL_TYPE;
	cb_data.percentage_done = 100;
	cb_data.message = NULL;
	om_cb(&cb_data, 0);
}

static void
read_and_save_locale(char *path)
{
	char lc_collate[MAX_LOCALE];
	char lc_ctype[MAX_LOCALE];
	char lc_messages[MAX_LOCALE];
	char lc_monetary[MAX_LOCALE];
	char lc_numeric[MAX_LOCALE];
	char lc_time[MAX_LOCALE];
	char lang[MAX_LOCALE];
	FILE 	*tmpfp = NULL;
	FILE	*deffp = NULL;

	if (path[0] == '\0')
		return;

	tmpfp = fopen(path, "r");
	if (tmpfp == NULL)
		return;

	(void) read_locale_file(tmpfp, lang, lc_collate, lc_ctype,
	    lc_messages, lc_monetary, lc_numeric, lc_time);

	(void) fclose(tmpfp);

	deffp = fopen(TMP_DEFSYSLOC, "w");
	if (deffp == NULL) {
		return;
	}

	/*
	 * Don't care about error. If error, then system will behave
	 * as it does currently during SUUpgrade.
	 */
	fprintf(deffp, "%s\n", lc_ctype);
	(void) fclose(deffp);
}

/*
 * Add swap entry to /etc/vfstab
 */
static void
setup_etc_vfstab_for_swap(char *target)
{
	FILE	*fp;
	char	cmd[MAXPATHLEN];

	if (target == NULL) {
		return;
	}

	(void) snprintf(cmd, sizeof (cmd), "%s/etc/vfstab", target);

	fp = fopen(cmd, "a+");
	if (fp == NULL) {
		om_log_print("Cannot open %s to add entry for swap\n", cmd);
		return;
	}

	om_log_print("Setting up swap mount in %s\n", cmd);

	(void) fprintf(fp, "%s\t%s\t\t%s\t\t%s\t%s\t%s\t%s\n",
	    swap_device, "-", "-", "swap", "-", "no", "-");

	(void) fclose(fp);
}

/*
 * Setup mountpoint property back to "/" from "/a" for
 * /, /opt, /export, /export/home
 */
static void
reset_zfs_mount_property(char *target)
{
	char 		cmd[MAXPATHLEN];
	nvlist_t	*attrs;
	int		i, ret;

	if (target == NULL) {
		return;
	}

	om_log_print("Unmounting BE\n");

	/*
	 * make sure we are not in alternate root
	 * otherwise be_unmount() fails
	 */

	chdir("/root");

	/*
	 * Since be_unmount() can't currently handle shared filesystems,
	 * it is necessary to manually set their mountpoint to the
	 * appropriate value.
	 */

	for (i = l_zfs_shared_fs_num - 1; i >= 0; i--) {
		(void) snprintf(cmd, sizeof (cmd),
		    "/usr/sbin/zfs unmount %s%s",
		    ROOTPOOL_NAME, zfs_shared_fs_names[i]);

		om_log_print("%s\n", cmd);
		ret = td_safe_system(cmd, B_TRUE);

		if ((ret == -1) || WEXITSTATUS(ret) != 0) {
			om_debug_print(OM_DBGLVL_WARN,
			    "Couldn't unmount %s%s, err=%d\n", ROOTPOOL_NAME,
			    zfs_shared_fs_names[i], ret);
		}

		(void) snprintf(cmd, sizeof (cmd),
		    "/usr/sbin/zfs set mountpoint=%s %s%s",
		    zfs_shared_fs_names[i], ROOTPOOL_NAME,
		    zfs_shared_fs_names[i]);

		om_log_print("%s\n", cmd);
		ret = td_safe_system(cmd, B_TRUE);

		if ((ret == -1) || WEXITSTATUS(ret) != 0) {
			om_debug_print(OM_DBGLVL_WARN,
			    "Couldn't change mountpoint for %s%s, err=%d\n",
			    ROOTPOOL_NAME, zfs_shared_fs_names[i], ret);
		}
	}

	ret = 0;

	/*
	 * Unmount non-shared BE filesystems
	 */
	if (nvlist_alloc(&attrs, NV_UNIQUE_NAME, 0) != 0) {
		om_log_print("Could not create target list.\n");

		ret = -1;
	} else if (nvlist_add_string(attrs, BE_ATTR_ORIG_BE_NAME,
	    INIT_BE_NAME) != 0) {
		om_log_print("BE name could not be added. \n");

		ret = -1;
		nvlist_free(attrs);
	}

	/*
	 * Transfer log files to the destination now,
	 * when everything is finished.
	 * In the next step, we are going to unmount target BE,
	 * so now is the last opportunity we could transfer anything
	 * to the target.
	 */

	if (ict_transfer_logs("/", target) != ICT_SUCCESS) {
		om_log_print("Failed to transfer install log file\n"
		    "%s\n", ICT_STR_ERROR(ict_errno));
	}

	if (ret == 0) {
		ret = be_unmount(attrs);
		nvlist_free(attrs);

		if (ret != BE_SUCCESS) {
			om_debug_print(OM_DBGLVL_ERR,
			    "Couldn't unmount target BE, be_unmount() returned "
			    "%d\n", ret);
		}
	}
}

/*
 * Setup bootfs property, so that newly created Solaris instance
 * is boooted appropriately
 */
static void
activate_be(char *be_name)
{
	char 		cmd[MAXPATHLEN];

	/*
	 * Set bootfs property for root pool. It can't be
	 * set before root filesystem is created.
	 */

	(void) snprintf(cmd, sizeof (cmd),
	    "/usr/sbin/zpool set bootfs=%s/ROOT/%s %s",
	    ROOTPOOL_NAME, be_name, ROOTPOOL_NAME);

	om_log_print("%s\n", cmd);
	td_safe_system(cmd, B_TRUE);
}

/*
 * Execute install-finish script to complete setup.
 */
static void
run_install_finish_script(char *target, char *uname, char *lname,
    char *upasswd, char *rpasswd)
{
	char cmd[1024];
	char *tool = "/sbin/install-finish ";

	if (target == NULL) {
		return;
	}
	om_log_print("Running install-finish script\n");
	(void) snprintf(cmd, sizeof (cmd),
	    "%s -B '%s' -R '%s' -n '%s' -l '%s' -p '%s' "
	    "-G '%d' -U '%d'",
	    tool, target, rpasswd, uname, lname, upasswd,
	    ICT_USER_GID, ICT_USER_UID);

	om_log_print("%s\n", cmd);
	td_safe_system(cmd, B_FALSE);
}

/*
 * prepare_zfs_root_pool_attrs
 * Creates nvlist set of attributes describing ZFS pool to be created/released
 * Input:	nvlist_t **attrs - attributes describing the target
 *		char *disk_name - disk name which will hold the pool
 * Output:
 * Return:	OM_SUCCESS
 *		OM_FAILURE
 * Notes:
 */
static int
prepare_zfs_root_pool_attrs(nvlist_t **attrs, char *disk_name)
{
	if (nvlist_alloc(attrs, TI_TARGET_NVLIST_TYPE, 0) != 0) {
		om_log_print("Could not create target nvlist.\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_uint32(*attrs, TI_ATTR_TARGET_TYPE,
	    TI_TARGET_TYPE_ZFS_RPOOL) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_TARGET_TYPE to"
		    "nvlist\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string(*attrs, TI_ATTR_ZFS_RPOOL_NAME,
	    ROOTPOOL_NAME) != 0) {
		om_log_print("ZFS root pool name could not be added. \n");

		return (OM_FAILURE);
	}

	snprintf(zfs_device, sizeof (zfs_device), "%ss0", disk_name);

	if (nvlist_add_string(*attrs, TI_ATTR_ZFS_RPOOL_DEVICE,
	    zfs_device) != 0) {
		om_log_print("Could not set zfs rpool device name\n");

		return (OM_FAILURE);
	}

	return (OM_SUCCESS);
}

/*
 * prepare_zfs_volume_attrs
 * Creates nvlist set of attributes describing ZFS volumes to be created
 * Two ZFS volumes are to be created - one for swap, one for dump
 * Input:	nvlist_t **attrs - attributes describing the target
 *		available_disk_space - space which can be dedicated to
 *			swap and dump.
 *		create_min_swap_only - only swap with minimum size is created
 *
 * Output:
 * Return:	OM_SUCCESS
 *		OM_FAILURE
 * Notes:
 */
static int
prepare_zfs_volume_attrs(nvlist_t **attrs, uint64_t available_disk_space,
    boolean_t create_min_swap_only)
{
	uint16_t	vol_num = create_min_swap_only ? 1 : 2;
	char		*vol_names[2] = {TI_ZFS_VOL_NAME_SWAP,
	    TI_ZFS_VOL_NAME_DUMP};
	uint32_t	vol_sizes[2];
	uint16_t	vol_types[2] = {TI_ZFS_VOL_TYPE_SWAP,
	    TI_ZFS_VOL_TYPE_DUMP};

	if (nvlist_alloc(attrs, TI_TARGET_NVLIST_TYPE, 0) != 0) {
		om_log_print("Could not create target nvlist.\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_uint32(*attrs, TI_ATTR_TARGET_TYPE,
	    TI_TARGET_TYPE_ZFS_VOLUME) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_TARGET_TYPE to "
		    "nvlist\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string(*attrs, TI_ATTR_ZFS_VOL_POOL_NAME, ROOTPOOL_NAME)
	    != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_ZFS_VOL_POOL_NAME to "
		    "nvlist\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_uint16(*attrs, TI_ATTR_ZFS_VOL_NUM, vol_num) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_ZFS_VOL_NUM to "
		    "nvlist\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string_array(*attrs, TI_ATTR_ZFS_VOL_NAMES, vol_names,
	    vol_num) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_ZFS_VOL_NAMES to "
		    "nvlist\n");

		return (OM_FAILURE);
	}

	/* calculate size of swap and/or dump volumes */

	if (create_min_swap_only) {
		vol_sizes[0] = MIN_SWAP_SIZE;
	} else {
		vol_sizes[0] = calc_swap_size((available_disk_space *
		    MIN_SWAP_SIZE) / (MIN_SWAP_SIZE + MIN_DUMP_SIZE));

		vol_sizes[1] = calc_dump_size((available_disk_space *
		    MIN_DUMP_SIZE) / (MIN_SWAP_SIZE + MIN_DUMP_SIZE));
	}

	if (nvlist_add_uint32_array(*attrs, TI_ATTR_ZFS_VOL_MB_SIZES,
	    vol_sizes, vol_num) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_ZFS_VOL_SIZES to "
		    "nvlist\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_uint16_array(*attrs, TI_ATTR_ZFS_VOL_TYPES,
	    vol_types, vol_num) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_ZFS_VOL_TYPES to "
		    "nvlist\n");

		return (OM_FAILURE);
	}

	return (OM_SUCCESS);
}

/*
 * prepare_be_attrs
 * Creates nvlist set of attributes describing boot environment (BE)
 * Input:	nvlist_t **attrs - attributes describing the target
 *
 * Output:
 * Return:	OM_SUCCESS
 *		OM_FAILURE
 * Notes:
 */
static int
prepare_be_attrs(nvlist_t **attrs)
{
	if (nvlist_alloc(attrs, TI_TARGET_NVLIST_TYPE, 0) != 0) {
		om_log_print("Could not create target nvlist.\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_uint32(*attrs, TI_ATTR_TARGET_TYPE,
	    TI_TARGET_TYPE_BE) != 0) {
		(void) om_log_print("Couldn't add TI_ATTR_TARGET_TYPE to"
		    "nvlist\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string(*attrs, TI_ATTR_BE_RPOOL_NAME,
	    ROOTPOOL_NAME) != 0) {
		om_log_print("BE root pool name could not be added. \n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string(*attrs, TI_ATTR_BE_NAME,
	    INIT_BE_NAME) != 0) {
		om_log_print("BE name could not be added. \n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string_array(*attrs, TI_ATTR_BE_FS_NAMES,
	    zfs_fs_names, ZFS_FS_NUM) != 0) {
		om_log_print("Couldn't set zfs fs name attr\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string_array(*attrs, TI_ATTR_BE_SHARED_FS_NAMES,
	    zfs_shared_fs_names, l_zfs_shared_fs_num) != 0) {
		om_log_print("Couldn't set zfs shared fs name attr\n");

		return (OM_FAILURE);
	}

	if (nvlist_add_string(*attrs, TI_ATTR_BE_MOUNTPOINT,
	    INSTALLED_ROOT_DIR) != 0) {
		om_log_print("Couldn't set be mountpoint attr\n");

		return (OM_FAILURE);
	}

	return (OM_SUCCESS);
}


/*
 * obtain_image_info
 * Parse image info file and reads following information from it:
 * [1] total size of installed bits
 * [2] compression ratio, if ZFS compression is turned on
 * [3] compression type
 * Input:	image_info_t * info - pointer to structure, which will
 *		be populated with image information
 *
 * Output:
 * Return:	OM_SUCCESS - information read successfully
 *		OM_FAILURE - image information couldn't be obtained
 * Notes:
 */

static int
obtain_image_info(image_info_t *info)
{
	FILE		*info_file;
	char		line[IMAGE_INFO_LINE_MAXLN];
	boolean_t	got_size = B_FALSE;
	boolean_t	got_cratio = B_FALSE;
	boolean_t	got_ctype = B_FALSE;

	/*
	 * fill in the structure only once
	 */

	if (info->initialized) {
		return (OM_SUCCESS);
	}

	/*
	 * open image info file, parse it
	 * and populate data structure
	 */

	info_file = fopen(IMAGE_INFO_FILE_NAME, "r");
	if (info_file == NULL) {
		om_debug_print(OM_DBGLVL_WARN,
		    "Couldn't open image info file " IMAGE_INFO_FILE_NAME "\n");

		return (OM_FAILURE);
	}

	while (fgets(line, sizeof (line), info_file) != NULL) {
		char	*par_name, *par_value;

		/*
		 * get parameter name
		 */

		/* TAB is one of separators */

		par_name = strtok(line, "= 	");

		if (par_name == NULL) {
			om_debug_print(OM_DBGLVL_WARN,
			    "Invalid parameter %s\n", line);
			continue;
		}

		/*
		 * get parameter value
		 */

		par_value = strtok(NULL, "= 	");

		if (par_value == NULL) {
			om_debug_print(OM_DBGLVL_WARN,
			    "Invalid parameter %s\n", line);
			continue;
		}

		/*
		 * look at the parameter name and compare to
		 * known/requested
		 */

		if (strcmp(par_name, IMAGE_INFO_TOTAL_SIZE) == 0) {
			uint64_t	size;

			errno = 0;
			size = strtoll(par_value, NULL, 10);

			if (errno == 0) {
				om_debug_print(OM_DBGLVL_INFO,
				    "Got image size: %lld\n", size);

				/* convert kiB -> MiB */

				image_info.image_size = size/ONE_MB_TO_KB;
				got_size = B_TRUE;
			} else
				om_debug_print(OM_DBGLVL_WARN,
				    "Invalid format of total size parameter\n");
		}

		/*
		 * ask for compression ratio
		 */

		if (strcmp(par_name, IMAGE_INFO_COMPRESSION_RATIO) == 0) {
			float ratio;

			errno = 0;
			ratio = strtof(par_value, NULL);

			if (errno == 0) {
				om_debug_print(OM_DBGLVL_INFO,
				    "Got compression ratio: %f\n", ratio);

				image_info.compress_ratio = ratio;
				got_cratio = B_TRUE;
			} else
				om_debug_print(OM_DBGLVL_WARN,
				    "Invalid format of compression ratio "
				    "parameter\n");
		}

		/*
		 * ask for compression type
		 */

		if (strcmp(par_name, IMAGE_INFO_COMPRESSION_TYPE) == 0) {
			char *type;

			type = strdup(par_value);

			if (type != NULL) {
				om_debug_print(OM_DBGLVL_INFO,
				    "Got compression type: %s\n", type);

				image_info.compress_type = type;
				got_ctype = B_TRUE;
			} else
				om_debug_print(OM_DBGLVL_WARN,
				    "Invalid format of compression type "
				    "parameter\n");
		}

		if (got_size && got_cratio && got_ctype)
			break;
	}

	(void) fclose(info_file);

	/*
	 * if at least one of parameters obtained,
	 * we read image info file successfully
	 */

	if (got_size || got_cratio || got_ctype) {
		info->initialized = B_TRUE;
		return (OM_SUCCESS);
	} else
		return (OM_FAILURE);
}


/*
 * get_dataset_property
 *
 * Obtains value of dataset property
 * Return:	== NULL - couldn't obtain property
 *		!= NULL - pointer to property value
 * Notes:
 */

static char *
get_dataset_property(char *dataset_name, char *property)
{
	FILE	*p;
	char	cmd[MAXPATHLEN];
	char	*strbuf;
	int	ret;

	strbuf = malloc(MAXPATHLEN);

	if (strbuf == NULL) {
		om_log_print("Out of memory\n");
		return (NULL);
	}

	(void) snprintf(cmd, sizeof (cmd),
	    "/usr/sbin/zfs get -H -o value %s %s", property, dataset_name);

	om_log_print("%s\n", cmd);

	if ((p = popen(cmd, "r")) == NULL) {
		om_log_print("Couldn't obtain dataset property\n");

		free(strbuf);
		return (NULL);
	}

	if (fgets(strbuf, MAXPATHLEN, p) == NULL) {
		om_log_print("Couldn't obtain dataset property\n");

		free(strbuf);
		(void) pclose(p);
		return (NULL);
	}

	ret = WEXITSTATUS(pclose(p));

	if (ret != 0) {
		om_log_print("Command failed with error %d\n", ret);
		free(strbuf);
		return (NULL);
	}

	/* strip new line */
	if (strbuf[strlen(strbuf) - 1] == '\n')
		strbuf[strlen(strbuf) - 1] = '\0';

	return (strbuf);
}


/*
 * get_available_disk_space
 *
 * Obtains information about real disk space available for installation
 * by taking a look at ZFS "available" attribute of pool root dataset
 *
 * Return:	0  - couldn't obtain available disk space
 *		>0 - available disk space in MiB
 * Notes:
 */

static uint64_t
get_available_disk_space(void)
{
	FILE		*p;
	char		cmd[MAXPATHLEN];
	char		*strbuf;
	int		ret;
	uint64_t	avail_space;

	strbuf = malloc(MAXPATHLEN);

	if (strbuf == NULL) {
		om_log_print("Out of memory\n");
		return (0);
	}

	(void) snprintf(cmd, sizeof (cmd),
	    "/usr/sbin/zfs get -Hp -o value available " ROOTPOOL_NAME);

	om_log_print("%s\n", cmd);

	if ((p = popen(cmd, "r")) == NULL) {
		om_log_print("Couldn't obtain available space\n");

		free(strbuf);
		return (0);
	}

	if (fgets(strbuf, MAXPATHLEN, p) == NULL) {
		om_log_print("Couldn't obtain available space\n");

		free(strbuf);
		(void) pclose(p);
		return (0);
	}

	ret = WEXITSTATUS(pclose(p));

	if (ret != 0) {
		om_log_print("Command failed with error %d\n", ret);
		free(strbuf);
		return (0);
	}

	/* strip new line */
	if (strbuf[strlen(strbuf) - 1] == '\n')
		strbuf[strlen(strbuf) - 1] = '\0';

	om_debug_print(OM_DBGLVL_INFO,
	    ROOTPOOL_NAME " pool: %s bytes are available\n", strbuf);

	/* convert to MiB */
	errno = 0;
	avail_space = strtoll(strbuf, NULL, 10) / ONE_MB_TO_BYTE;
	if (errno != 0) {
		om_log_print("Couldn't obtain available space, strtoll() "
		"failed with error %d\n", errno);
		free(strbuf);
		return (0);
	}

	om_debug_print(OM_DBGLVL_INFO,
	    ROOTPOOL_NAME " pool: %llu MiB are available\n", avail_space);

	return (avail_space);
}


/*
 * get_recommended_size_for_software
 *
 * Calculates recommended disk size for software portion
 * of installation
 *
 * Return:	0  - couldn't obtain available disk space
 *		>0 - available disk space in MiB
 * Notes:
 */

static uint64_t
get_recommended_size_for_software(void)
{
	/*
	 * Size in MB that is the recommended device size we will allow
	 * for installing Slim.
	 *
	 * Account for one full upgrade and add another 2 GiB for additional
	 * software.
	 *
	 * If minimum disk size accounts for swap, exclude it.
	 */

	return ((om_get_min_size(NULL, NULL) - calc_required_swap_size()) * 2 +
	    2048);
}
