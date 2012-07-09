/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright © 2000-2003 Marco Pesenti Gritti
 *  Copyright © 2011 Igalia S.L.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__EPHY_EPIPHANY_H_INSIDE__) && !defined (EPIPHANY_COMPILATION)
#error "Only <epiphany/epiphany.h> can be included directly."
#endif

#ifndef EPHY_EMBED_SHELL_H
#define EPHY_EMBED_SHELL_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "ephy-download.h"

G_BEGIN_DECLS

#define EPHY_TYPE_EMBED_SHELL		(ephy_embed_shell_get_type ())
#define EPHY_EMBED_SHELL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), EPHY_TYPE_EMBED_SHELL, EphyEmbedShell))
#define EPHY_EMBED_SHELL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), EPHY_TYPE_EMBED_SHELL, EphyEmbedShellClass))
#define EPHY_IS_EMBED_SHELL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), EPHY_TYPE_EMBED_SHELL))
#define EPHY_IS_EMBED_SHELL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), EPHY_TYPE_EMBED_SHELL))
#define EPHY_EMBED_SHELL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), EPHY_TYPE_EMBED_SHELL, EphyEmbedShellClass))

typedef struct _EphyEmbedShellClass	EphyEmbedShellClass;
typedef struct _EphyEmbedShell		EphyEmbedShell;
typedef struct _EphyEmbedShellPrivate	EphyEmbedShellPrivate;

extern EphyEmbedShell *embed_shell;

typedef enum
{
	EPHY_EMBED_SHELL_MODE_BROWSER,
	EPHY_EMBED_SHELL_MODE_PRIVATE,
	EPHY_EMBED_SHELL_MODE_APPLICATION,
	EPHY_EMBED_SHELL_MODE_TEST
} EphyEmbedShellMode;

struct _EphyEmbedShell
{
	GtkApplication parent;

	/*< private >*/
	EphyEmbedShellPrivate *priv;
};

struct _EphyEmbedShellClass
{
	GtkApplicationClass parent_class;

	void	  (* download_added)	(EphyEmbedShell *shell, EphyDownload *download);
	void	  (* download_removed)	(EphyEmbedShell *shell, EphyDownload *download);

	void	  (* prepare_close)	(EphyEmbedShell *shell);

	/*< private >*/
	GObject * (* get_embed_single)  (EphyEmbedShell *shell);
};

GType		   ephy_embed_shell_get_type		(void);

EphyEmbedShell	  *ephy_embed_shell_get_default		(void);

GObject		  *ephy_embed_shell_get_global_history_service (EphyEmbedShell *shell);

GObject		  *ephy_embed_shell_get_encodings	(EphyEmbedShell *shell);

GObject		  *ephy_embed_shell_get_embed_single	(EphyEmbedShell *shell);

GObject        	  *ephy_embed_shell_get_adblock_manager	(EphyEmbedShell *shell);

void		   ephy_embed_shell_prepare_close	(EphyEmbedShell *shell);

void		   ephy_embed_shell_set_page_setup	(EphyEmbedShell *shell,
							 GtkPageSetup *page_setup);
		
GtkPageSetup	  *ephy_embed_shell_get_page_setup	(EphyEmbedShell *shell);

void		   ephy_embed_shell_set_print_settings	(EphyEmbedShell *shell,
							 GtkPrintSettings *settings);
		
GtkPrintSettings  *ephy_embed_shell_get_print_settings	(EphyEmbedShell *shell);

GList		  *ephy_embed_shell_get_downloads	(EphyEmbedShell *shell);

void		   ephy_embed_shell_add_download	(EphyEmbedShell *shell,
							 EphyDownload *download);
void		   ephy_embed_shell_remove_download	(EphyEmbedShell *shell,
							 EphyDownload *download);

EphyEmbedShellMode ephy_embed_shell_get_mode            (EphyEmbedShell *shell);
const char *       ephy_embed_shell_get_app_mode_origin (EphyEmbedShell *shell);

G_END_DECLS

#endif /* !EPHY_EMBED_SHELL_H */
