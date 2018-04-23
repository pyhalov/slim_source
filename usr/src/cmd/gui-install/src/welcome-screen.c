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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "callbacks.h"
#include "installation-profile.h"
#include "interface-globals.h"
#include "window-graphics.h"
#include "welcome-screen.h"
#include "help-dialog.h"

#define XDG_OPEN "/usr/bin/xdg-open"

/*
 * Signal handler connected up by Glade XML signal autoconnect
 * for the release notes button clicked event.
 */
gboolean
on_releasenotesbutton_clicked(GtkWidget *widget,
		gpointer user_data)
{
	GError *error = NULL;
	gboolean result;
	uid_t suid;
	int pid;

	result = FALSE;
	/* The installer will typically be run as root under sudo,
           but we don't want to run browser as root */

	suid = geteuid();
	if (suid == 0) {
		char *sudo_uid;

		sudo_uid = getenv("SUDO_UID");
		if (sudo_uid)
			suid = strtol(sudo_uid, (char**)NULL, 10);
	}
	pid = fork();
	if (pid == 0) {
		if (suid > 0 && suid != geteuid()) {
			struct passwd *pw;

			setuid(suid);
			pw = getpwuid(suid);
			if (pw != NULL) {
				if (pw->pw_name != NULL) {
					setenv("USERNAME", pw->pw_name, 1);
					setenv("LOGNAME", pw->pw_name, 1);
				}
				if (pw->pw_dir != NULL) {
					setenv("HOME", pw->pw_dir, 1);
				}
			}
		}
		execl(XDG_OPEN, XDG_OPEN, RELEASENOTESURL, (char *)0);
		exit(-1);
	} else if (pid > 0) {
		int status;

		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
			result = TRUE;
		}
	}

	if (result != TRUE) {
		gui_install_prompt_dialog(
			FALSE,
			FALSE,
			FALSE,
			GTK_MESSAGE_ERROR,
			_("Unable to display release notes"),
			NULL);
		g_error_free(error);
	}
	return (TRUE);
}

void
welcome_screen_init(void)
{
	/*
	 * Welcome screen specific initialisation code.
	 */
	glade_xml_signal_autoconnect(MainWindow.welcomewindowxml);

	InstallationProfile.installationtype = INSTALLATION_TYPE_INITIAL_INSTALL;

	MainWindow.WelcomeWindow.welcomescreenvbox =
			glade_xml_get_widget(MainWindow.welcomewindowxml,
					"welcomescreenvbox");
	gtk_box_pack_start(GTK_BOX(MainWindow.screencontentvbox),
			MainWindow.WelcomeWindow.welcomescreenvbox, TRUE, TRUE, 0);

	MainWindow.WelcomeWindow.releasebutton =
			glade_xml_get_widget(MainWindow.welcomewindowxml,
					"releasenotesbutton");
	gtk_widget_grab_focus(MainWindow.WelcomeWindow.releasebutton);
}

/*
 * Set default widget with focus. For welcome screen,
 * the default widget is release notes button
 */
void
welcome_screen_set_default_focus(void)
{
	gtk_widget_grab_focus(MainWindow.WelcomeWindow.releasebutton);
}
