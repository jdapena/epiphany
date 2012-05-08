/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2012 Igalia S.L.
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

#include "config.h"
#include "ephy-back-to-web-app-action.h"

#include "ephy-embed-container.h"
#include "ephy-embed-shell.h"
#include "ephy-embed-utils.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

static void ephy_back_to_web_app_action_init       (EphyBackToWebAppAction *action);
static void ephy_back_to_web_app_action_class_init (EphyBackToWebAppActionClass *klass);

G_DEFINE_TYPE (EphyBackToWebAppAction, ephy_back_to_web_app_action, EPHY_TYPE_LINK_ACTION)

static void
action_activate (GtkAction *action)
{
  EphyWindow *window;
  EphyEmbed *embed;
  WebKitWebView *web_view;
  const char *last_app_uri;

  window = ephy_window_action_get_window (EPHY_WINDOW_ACTION (action));
  embed = ephy_embed_container_get_active_child (EPHY_EMBED_CONTAINER (window));
  g_return_if_fail (embed != NULL);

  web_view = EPHY_GET_WEBKIT_WEB_VIEW_FROM_EMBED (embed);

  last_app_uri = ephy_web_view_get_last_web_app_address (EPHY_WEB_VIEW (web_view));

  webkit_web_view_load_uri (web_view, last_app_uri);
  gtk_widget_grab_focus (GTK_WIDGET (embed));

}

static void
ephy_back_to_web_app_action_init (EphyBackToWebAppAction *action)
{
}

static void
ephy_back_to_web_app_action_class_init (EphyBackToWebAppActionClass *klass)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (klass);

  action_class->activate = action_activate;
}
