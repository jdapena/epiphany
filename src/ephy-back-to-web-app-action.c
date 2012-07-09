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
#ifdef HAVE_WEBKIT2
  WebKitBackForwardList *history;
  WebKitBackForwardListItem *back_item;
#else
  WebKitWebHistoryItem *back_item;
  WebKitWebBackForwardList *history;
#endif
  GList *items, *node;

  window = ephy_window_action_get_window (EPHY_WINDOW_ACTION (action));
  embed = ephy_embed_container_get_active_child (EPHY_EMBED_CONTAINER (window));
  g_return_if_fail (embed != NULL);

  web_view = EPHY_GET_WEBKIT_WEB_VIEW_FROM_EMBED (embed);
  history = webkit_web_view_get_back_forward_list (web_view);

#ifdef HAVE_WEBKIT2
  items = webkit_back_forward_list_get_back_list (history);
  node = items;
  while (node != NULL) {
    if (ephy_embed_shell_address_in_web_app_origin (ephy_embed_shell_get_default (),
                                                    webkit_back_forward_list_item_get_uri (back_item))) {
      webkit_web_view_go_to_back_forward_list_item (web_view,
                                                    back_item);
      break;
    } else {
      node = g_list_next (node);
    }
  }
#else
  items = webkit_web_back_forward_list_get_back_list_with_limit (history, G_MAXINT);
  node = items;
  while (node != NULL) {
    back_item = (WebKitWebHistoryItem *) node->data;
    if (ephy_embed_shell_address_in_web_app_origin (ephy_embed_shell_get_default (),
                                                    webkit_web_history_item_get_uri (back_item))) {
      webkit_web_view_go_to_back_forward_item (web_view,
                                               back_item);
      break;
    } else {
      node = g_list_next (node);
    }
  }
#endif

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
