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
UI element for representing a single activatable list item
'''

import logging
from osol_install.text_install.list_item import ListItem
from osol_install.text_install import LOG_LEVEL_INPUT

KEY_SPACE = ord(' ')


class MultiListItem(ListItem):
    '''
    Represents a single item selectable from a list
    '''
    def __init__(self, area, window=None, color_theme=None, color=None,
                 highlight_color=None, text="", centered=False, used=False, **kwargs):
        '''
        See also ListItem.__init__

        '''
        self.on_select_kwargs = {}
        self.on_select = None
        self.used = used
        super(MultiListItem, self).__init__(area=area, window=window,
                                       color_theme=color_theme,
                                       color=color,
                                       highlight_color=highlight_color,
                                       text=text,
                                       centered=centered,
                                       **kwargs)

    def _init_key_dict(self):
        '''Map some keystrokes by default

        '''

        super(MultiListItem, self)._init_key_dict()
        self.key_dict[KEY_SPACE] = self.on_space

    def on_space (self, input_key):
        '''On KEY_SPACE:
        Activate object action

        '''
        logging.log(LOG_LEVEL_INPUT, "MultiListItem.on_space\n%s", type(self))
        if self.on_select is not None:
           self.on_select(**self.on_select_kwargs)
        self.used = not self.used
        self.set_text(self.text, self.centered)
        self.no_ut_refresh()
        return input_key

    def set_text(self, text, centered=False):
        '''Set the text of this ListItem. Shortcut to InnerWindow.add_text
        ensures that this window is cleared first

        '''
        self.window.clear()
        if self.used:
           additional_text = '[*] '
        else:
           additional_text = '[ ] '
        self.add_text(additional_text + text, max_chars=(self.window.getmaxyx()[1] - 1),
                      centered=centered)
