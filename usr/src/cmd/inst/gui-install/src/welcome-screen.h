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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef __WELCOME_SCREEN_H
#define	__WELCOME_SCREEN_H

#pragma ident	"@(#)welcome-screen.h	1.1	07/08/03 SMI"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define	RELEASENOTESNODE "textviewdialog"

typedef struct _WelcomeWindowXML {
	GladeXML *welcomewindowxml;
	GladeXML *releasenotesxml;

	GtkWidget *welcomescreenvbox;
	GtkWidget *welcomesummarylabel;
	GtkWidget *releasenoteslabel;
	GtkWidget *releasenotesdialog;
	GtkWidget *releasenotesclosebutton;
	GtkWidget *releasenotestextview;
	GtkWidget *installradio;
	GtkWidget *upgraderadio;

	gint installationtype;

} WelcomeWindowXML;

void		welcome_screen_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __WELCOME_SCREEN_H */
