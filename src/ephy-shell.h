/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2000-2004 Marco Pesenti Gritti
 *  Copyright © 2003, 2004, 2006 Christian Persch
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

#ifndef EPHY_SHELL_H
#define EPHY_SHELL_H

#include "ephy-embed-shell.h"
#include "ephy-bookmarks.h"
#include "ephy-window.h"
#include "ephy-embed.h"
#include "ephy-web-application.h"

#include <webkit/webkit.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

#define EPHY_TYPE_SHELL         (ephy_shell_get_type ())
#define EPHY_SHELL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EPHY_TYPE_SHELL, EphyShell))
#define EPHY_SHELL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EPHY_TYPE_SHELL, EphyShellClass))
#define EPHY_IS_SHELL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EPHY_TYPE_SHELL))
#define EPHY_IS_SHELL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EPHY_TYPE_SHELL))
#define EPHY_SHELL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EPHY_TYPE_SHELL, EphyShellClass))

typedef struct _EphyShell   EphyShell;
typedef struct _EphyShellClass    EphyShellClass;
typedef struct _EphyShellPrivate  EphyShellPrivate;

extern EphyShell *ephy_shell;

typedef enum {
  /* Page types */
  EPHY_NEW_TAB_HOME_PAGE    = 1 << 0,
  EPHY_NEW_TAB_NEW_PAGE   = 1 << 1,
  EPHY_NEW_TAB_OPEN_PAGE    = 1 << 2,

  /* Page mode */
  EPHY_NEW_TAB_FULLSCREEN_MODE  = 1 << 4,
  EPHY_NEW_TAB_DONT_SHOW_WINDOW = 1 << 5,

  /* Tabs */
  EPHY_NEW_TAB_APPEND_LAST  = 1 << 7,
  EPHY_NEW_TAB_APPEND_AFTER = 1 << 8,
  EPHY_NEW_TAB_JUMP   = 1 << 9,
  EPHY_NEW_TAB_IN_NEW_WINDOW  = 1 << 10,
  EPHY_NEW_TAB_IN_EXISTING_WINDOW = 1 << 11,

  /* The way to load */
  EPHY_NEW_TAB_FROM_EXTERNAL      = 1 << 12,
  EPHY_NEW_TAB_DONT_COPY_HISTORY  = 1 << 13,
  
} EphyNewTabFlags;

typedef enum {
  EPHY_STARTUP_NEW_TAB          = 1 << 0,
  EPHY_STARTUP_NEW_WINDOW       = 1 << 1,
  EPHY_STARTUP_BOOKMARKS_EDITOR = 1 << 2
} EphyStartupFlags;

typedef struct {
  EphyStartupFlags startup_flags;
  
  char *bookmarks_filename;
  char *session_filename;
  char *bookmark_url;
  
  char **arguments;
  
  guint32 user_time;
} EphyShellStartupContext;

struct _EphyShell {
  EphyEmbedShell parent;

  /*< private >*/
  EphyShellPrivate *priv;
};

struct _EphyShellClass {
  EphyEmbedShellClass parent_class;
};

GType           ephy_new_tab_flags_get_type             (void) G_GNUC_CONST;

GType           ephy_shell_get_type                     (void);

EphyShell      *ephy_shell_get_default                  (void);

EphyEmbed      *ephy_shell_new_tab                      (EphyShell *shell,
                                                         EphyWindow *parent_window,
                                                         EphyEmbed *previous_embed,
                                                         const char *url,
                                                         EphyNewTabFlags flags);

EphyEmbed      *ephy_shell_new_tab_full                 (EphyShell *shell,
                                                         EphyWindow *parent_window,
                                                         EphyEmbed *previous_embed,
                                                         WebKitNetworkRequest *request,
                                                         EphyNewTabFlags flags,
                                                         EphyWebViewChrome chrome,
                                                         gboolean is_popup,
                                                         guint32 user_time);

GObject        *ephy_shell_get_session                  (EphyShell *shell);

EphyWebApplication *ephy_shell_get_application          (EphyShell *shell);
void            ephy_shell_set_application              (EphyShell *shell,
                                                         EphyWebApplication *app);

GObject        *ephy_shell_get_net_monitor              (EphyShell *shell);

EphyBookmarks  *ephy_shell_get_bookmarks                (EphyShell *shell);

GObject        *ephy_shell_get_extensions_manager       (EphyShell *shell);

GtkWidget      *ephy_shell_get_bookmarks_editor         (EphyShell *shell);

GtkWidget      *ephy_shell_get_history_window           (EphyShell *shell);

GObject        *ephy_shell_get_pdm_dialog               (EphyShell *shell);

GObject        *ephy_shell_get_prefs_dialog             (EphyShell *shell);

G_END_DECLS

#endif
