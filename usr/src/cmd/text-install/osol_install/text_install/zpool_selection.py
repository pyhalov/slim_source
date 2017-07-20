#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"). You may
# only use this file in accordance with the terms of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved
# Copyright 2017 Alexander Pyhalov
#

'''
Screens and functions to display a list of pools to the user.
'''

from copy import deepcopy
import curses
import logging
import platform
import re
import threading
import traceback

from osol_install.profile.disk_info import DiskInfo, SliceInfo
from osol_install.text_install import _, RELEASE
from osol_install.text_install.base_screen import BaseScreen, \
                                                  SkipException, \
                                                  QuitException, \
                                                  UIMessage
from osol_install.text_install.disk_window import DiskWindow, \
                                                  get_minimum_size, \
                                                  get_recommended_size
from osol_install.text_install.edit_field import EditField
from osol_install.text_install.error_window import ErrorWindow
from osol_install.text_install.i18n import fit_text_truncate, \
                                           textwidth, \
                                           ljust_columns
from osol_install.text_install.list_item import ListItem
from osol_install.text_install.multi_list_item import MultiListItem
from osol_install.text_install.scroll_window import ScrollWindow
from osol_install.text_install.window_area import WindowArea
from osol_install.text_install.ti_install_utils import get_zpool_list, \
                                                       get_zpool_free_size, \
                                                       get_zpool_be_names
class ZpoolScreen(BaseScreen):
    '''
    Allow the user to select a (valid) zpool target for installation
    '''
    
    HEADER_TEXT = _("Pools")
    PARAGRAPH = _("Where should %(release)s be installed?") % RELEASE
    SIZE_TEXT = _("Recommended size:  %(recommend).1fGB      "
                  "Minimum size: %(min).1fGB")
    POOL_SEEK_TEXT = _("Seeking pools on system")
    TOO_SMALL = _("Too small")
    NO_POOLS = _("No pools found. ")
    NO_TARGETS = _("%(release)s cannot be installed on any pool") % RELEASE
    TGT_ERROR = _("An error occurred while searching for installation"
                  " targets. Please check the install log and file a bug"
                  " at bugs.openindiana.org.")
    OVERWRITE_BOOT_CONFIGURATION_LABEL = _("Overwrite pool's boot configuration")
    BE_LABEL = _("Select BE name:")
    FILESYSTEM_EXISTS_ERROR = _("ZFS file system"
                  " %(pool_name)s/ROOT/%(be_name)s already exists")
    
    POOL_HEADERS = [(25, _("Name")),
                    (10, _("Size(GB)")),
                    (16, _("Notes"))]

    BE_SCREEN_LEN = 32
    ITEM_OFFSET = 2
    
    def __init__(self, main_win):
        super(ZpoolScreen, self).__init__(main_win)
        
        pool_header_text = []
        for header in ZpoolScreen.POOL_HEADERS:
            header_str = fit_text_truncate(header[1], header[0]-1, just="left")
            pool_header_text.append(header_str)
        self.pool_header_text = " ".join(pool_header_text)
        
        self.existing_pools = []
        self.num_targets = 0
        max_note_size = ZpoolScreen.POOL_HEADERS[2][0]
        self.too_small_text = ZpoolScreen.TOO_SMALL[:max_note_size]
        self._size_line = None
        self.selected_pool = 0
        self._minimum_size = None
        self._recommended_size = None
        self.pool_win = None

        max_field = textwidth(ZpoolScreen.BE_LABEL)

        self.max_text_len = (self.win_size_x - ZpoolScreen.BE_SCREEN_LEN -
                             ZpoolScreen.ITEM_OFFSET) / 2
        self.text_len = min(max_field + 1, self.max_text_len)
        self.list_area = WindowArea(1, self.text_len, 0,
                                    ZpoolScreen.ITEM_OFFSET)
        
        self.edit_area = WindowArea(1, ZpoolScreen.BE_SCREEN_LEN + 1,
                                    0, self.text_len)
        err_x_loc = 2 * self.max_text_len - self.text_len
        err_width = (self.text_len + ZpoolScreen.BE_SCREEN_LEN)
        self.error_area = WindowArea(1, err_width, 0, err_x_loc)
        self.be_name_list = None
        self.be_name_edit = None
        self.be_name_err = None

        self.boot_configuration_item = None
        self.do_copy = False # Flag indicating if install_profile.disks
                             # should be copied
    
    def determine_minimum(self):
        '''Returns minimum install size, fetching first if needed'''
        self.determine_size_data()
        return self._minimum_size
    
    minimum_size = property(determine_minimum)
    
    def determine_recommended(self):
        '''Returns recommended install size, fetching first if needed'''
        self.determine_size_data()
        return self._recommended_size
    
    recommended_size = property(determine_recommended)
    
    def determine_size_data(self):
        '''Retrieve the minimum and recommended sizes and generate the string
        to present that information.
        
        '''
        if self._minimum_size is None or self._recommended_size is None:
            self._recommended_size = get_recommended_size().size_as("gb")
            self._minimum_size = get_minimum_size().size_as("gb")
    
    def get_size_line(self):
        '''Returns the line of text displaying the min/recommended sizes'''
        if self._size_line is None:
            size_dict = {"recommend" : self.recommended_size,
                         "min" : self.minimum_size}
            self._size_line = ZpoolScreen.SIZE_TEXT % size_dict
        return self._size_line
    
    size_line = property(get_size_line)

    def _show(self):
        '''Create a list of disks to choose from and create the window
        for displaying the partition/slice information from the selected
        disk
        
        '''
        if not self.install_profile.install_to_pool:
            raise SkipException

        if len(self.existing_pools) == 0:
            self.existing_pools.extend(get_zpool_list())
        self.num_targets = 0
        
        if len(self.existing_pools) == 0:
            self.center_win.add_paragraph(ZpoolScreen.NO_POOLS, 1, 1,
                                          max_x=(self.win_size_x - 1))
            return
        
        for pool in self.existing_pools:
            free_gb = get_zpool_free_size(pool)/1024/1024/1024
            if (get_zpool_free_size(pool)/1024/1024/1024 > self.minimum_size):
                self.num_targets += 1
            else:
                logging.info("Skipping pool %s: need %d GB, free %d GB" %
                    (pool, self.minimum_size, free_gb))
        
        if self.num_targets == 0:
            self.center_win.add_paragraph(ZpoolScreen.NO_TARGETS, 1, 1,
                                          max_x=(self.win_size_x - 1))
            return
        
        self.main_win.reset_actions()
        self.main_win.show_actions()
        
        y_loc = 1
        self.center_win.add_text(ZpoolScreen.PARAGRAPH, y_loc, 1)
        
        y_loc += 1
        self.center_win.add_text(self.size_line, y_loc, 1)
        
        y_loc += 2
        self.center_win.add_text(self.pool_header_text, y_loc, 1)
        
        y_loc += 1
        self.center_win.window.hline(y_loc, self.center_win.border_size[1] + 1,
                                     curses.ACS_HLINE,
                                     textwidth(self.pool_header_text))
        
        y_loc += 1
        pool_win_area = WindowArea(4, textwidth(self.pool_header_text) + 2,
                                   y_loc, 0)
        pool_win_area.scrollable_lines = len(self.existing_pools) + 1
        self.pool_win = ScrollWindow(pool_win_area,
                                     window=self.center_win)
        
        pool_item_area = WindowArea(1, pool_win_area.columns - 2, 0, 1)
        pool_index = 0
        len_name = ZpoolScreen.POOL_HEADERS[0][0] - 1
        len_size = ZpoolScreen.POOL_HEADERS[2][0] - 1
        for pool in self.existing_pools:
            pool_text_fields = []
            name_field = pool[:len_name]
            name_field = ljust_columns(name_field, len_name)
            pool_text_fields.append(name_field)
            pool_size = get_zpool_free_size(pool)/1024/1024/1024
            size_field = "%*.1f" % (len_size, pool_size)
            pool_text_fields.append(size_field)
            selectable = True
            if pool_size < self.minimum_size:
                note_field = self.too_small_text
                selectable = False
            else:
                note_field = ""
            pool_text_fields.append(note_field)
            pool_text = " ".join(pool_text_fields)
            pool_item_area.y_loc = pool_index
            pool_list_item = ListItem(pool_item_area, window=self.pool_win,
                                      text=pool_text, add_obj=selectable)
            pool_list_item.on_make_active = on_activate
            pool_list_item.on_make_active_kwargs["pool_select"] = self

            pool_index += 1
        self.pool_win.no_ut_refresh()
 
        y_loc += 7
        self.list_area.y_loc = y_loc
        self.error_area.y_loc = y_loc
        self.be_name_err =  ErrorWindow(self.error_area,
                                        window=self.center_win)
        self.be_name_list = ListItem(self.list_area, window=self.center_win,
                                     text=ZpoolScreen.BE_LABEL)
        self.be_name_edit = EditField(self.edit_area,
                                      window=self.be_name_list,
                                      validate=be_name_valid,
                                      error_win=self.be_name_err,
                                      text=self.install_profile.be_name)
         
        y_loc += 2
        boot_configuration_width = textwidth(ZpoolScreen.OVERWRITE_BOOT_CONFIGURATION_LABEL) + 5
        cols = (self.win_size_x - boot_configuration_width) / 2
        boot_configuration_area = WindowArea(1, boot_configuration_width, y_loc, cols)

        self.boot_configuration_item = MultiListItem(boot_configuration_area,
                                                     window=self.center_win,
                                                     text=ZpoolScreen.OVERWRITE_BOOT_CONFIGURATION_LABEL,
                                                     used=self.install_profile.overwrite_boot_configuration)
        
        self.boot_configuration_item.on_select = on_select_obc
        self.boot_configuration_item.on_select_kwargs["pool_select"] = self

        self.main_win.do_update()
        self.center_win.activate_object(self.pool_win)
        self.pool_win.activate_object(self.selected_pool)
        # Set the flag so that the pool is not copied by on_change_screen,
        # unless on_activate gets called as a result of the user changing
        # the selected pool.
        self.do_copy = False

    def on_change_screen(self):
        ''' Assign the selected pool to the InstallProfile, and make note of
        its index (in case the user returns to this screen later)
        
        '''
        if self.do_copy or self.install_profile.pool_name is None:
            self.install_profile.pool_name = self.existing_pools[self.pool_win.active_object]
        self.selected_pool = self.pool_win.active_object
        self.install_profile.be_name = self.be_name_edit.get_text()

    def validate(self):
        pool_name = self.existing_pools[self.pool_win.active_object]
        be_name = self.be_name_edit.get_text()
        if not be_name:
            raise UIMessage, _("Boot environment name is empty")

        be_names = get_zpool_be_names(pool_name)
        if be_name in be_names:
            filesystem_dict = {"pool_name": pool_name, "be_name": be_name}
            raise UIMessage, ZpoolScreen.FILESYSTEM_EXISTS_ERROR % filesystem_dict

def on_activate(pool_select=None):
    '''When a disk is selected, pass its data to the disk_select_screen'''

    # User selected a different disk; set the flag so that it gets copied
    # later
    pool_select.do_copy = True

def be_name_valid(edit_field):
    '''Ensure BE name is valid'''

    be_name = edit_field.get_text()
    if not be_name:
        raise UIMessage, _("Boot environment name is empty")
    
    search = re.compile(r'[^a-zA-Z0-9_\-]').search
    if bool(search(be_name)):
        raise UIMessage, _("Boot environment name contains unallowed symbols")

    return True

def on_select_obc(pool_select=None):
   if pool_select.install_profile.overwrite_boot_configuration is None:
       pool_select.install_profile.overwrite_boot_configuration = True
   else:
       pool_select.install_profile.overwrite_boot_configuration = \
           not pool_select.install_profile.overwrite_boot_configuration
