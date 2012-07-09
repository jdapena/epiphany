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
#include "ephy-toolbar.h"

#include "ephy-file-helpers.h"
#include "ephy-location-entry.h"
#include "ephy-middle-clickable-button.h"
#include "ephy-private.h"

G_DEFINE_TYPE (EphyToolbar, ephy_toolbar, GTK_TYPE_TOOLBAR)

#define EPHY_TOOLBAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), EPHY_TYPE_TOOLBAR, EphyToolbarPrivate))

enum {
  PROP_0,
  PROP_WINDOW,
  N_PROPERTIES
};

static GParamSpec *object_properties[N_PROPERTIES] = { NULL, };

struct _EphyToolbarPrivate {
  EphyWindow *window;
  GtkWidget *entry;
};

static void
ephy_toolbar_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
  EphyToolbarPrivate *priv = EPHY_TOOLBAR (object)->priv;

  switch (property_id) {
  case PROP_WINDOW:
    priv->window = EPHY_WINDOW (g_value_get_object (value));
    g_object_notify_by_pspec (object, object_properties[PROP_WINDOW]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ephy_toolbar_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
  EphyToolbarPrivate *priv = EPHY_TOOLBAR (object)->priv;

  switch (property_id) {
  case PROP_WINDOW:
    g_value_set_object (value, priv->window);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ephy_toolbar_constructed (GObject *object)
{
  EphyToolbarPrivate *priv = EPHY_TOOLBAR (object)->priv;
  GtkActionGroup *action_group;
  GtkAction *action;
  GtkToolItem *back_to_app, *back_forward, *location_stop_reload, *tool_item;
  GtkWidget *tool_button, *box, *location, *toolbar;
  GtkSizeGroup *size;
  GtkWidget *back_to_application_icon;
  gchar *app_icon_filename;
  GdkPixbuf *back_to_application_pixbuf;
  int icon_width, icon_height;

  G_OBJECT_CLASS (ephy_toolbar_parent_class)->constructed (object);

  toolbar = GTK_WIDGET (object);

  /* Create a GtkSizeGroup to sync the height of the location entry, and
   * the stop/reload button. */
  size = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

  /* Set the MENUBAR style class so it's possible to drag the app
   * using the toolbar. */
  gtk_style_context_add_class (gtk_widget_get_style_context (toolbar),
                               GTK_STYLE_CLASS_MENUBAR);

  /* Back to application*/

  app_icon_filename = g_build_filename (ephy_dot_dir (), "app-icon.png", NULL);
  gtk_icon_size_lookup (GTK_ICON_SIZE_BUTTON, &icon_width, &icon_height);
  back_to_application_pixbuf = gdk_pixbuf_new_from_file_at_size (app_icon_filename,
                                                                 icon_width, icon_height,
                                                                 NULL);
  back_to_application_icon = gtk_image_new_from_pixbuf (back_to_application_pixbuf);
  g_object_unref (back_to_application_pixbuf);
  g_free (app_icon_filename);
  back_to_app = gtk_tool_button_new (back_to_application_icon, NULL);
  gtk_tool_item_set_is_important (back_to_app, TRUE);

  action_group = ephy_window_get_toolbar_action_group (priv->window);
  action = gtk_action_group_get_action (action_group, "NavigationBackToApplication");
  gtk_action_set_always_show_image (action, TRUE);

  gtk_activatable_set_related_action (GTK_ACTIVATABLE (back_to_app),
                                      action);
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (back_to_app));
  gtk_widget_show_all (GTK_WIDGET (back_to_app));
  gtk_widget_set_margin_right (GTK_WIDGET (back_to_app), 12);

  /* Back and Forward */
  back_forward = gtk_tool_item_new ();
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  /* Back */
  tool_button = ephy_middle_clickable_button_new ();
  /* FIXME: apparently we need an image inside the button for the action
   * icon to appear. */
  gtk_button_set_image (GTK_BUTTON (tool_button), gtk_image_new ());
  action_group = ephy_window_get_toolbar_action_group (priv->window);
  action = gtk_action_group_get_action (action_group, "NavigationBack");
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (tool_button),
                                      action);
  gtk_button_set_label (GTK_BUTTON (tool_button), NULL);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (tool_button));

  /* Forward */
  tool_button = ephy_middle_clickable_button_new ();
  /* FIXME: apparently we need an image inside the button for the action
   * icon to appear. */
  gtk_button_set_image (GTK_BUTTON (tool_button), gtk_image_new ());
  action = gtk_action_group_get_action (action_group, "NavigationForward");
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (tool_button),
                                      action);
  gtk_button_set_label (GTK_BUTTON (tool_button), NULL);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (tool_button));

  gtk_style_context_add_class (gtk_widget_get_style_context (box),
                               "raised");
  gtk_style_context_add_class (gtk_widget_get_style_context (box),
                               "linked");

  gtk_container_add (GTK_CONTAINER (back_forward), box);
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (back_forward));
  gtk_widget_show_all (GTK_WIDGET (back_forward));
  gtk_widget_set_margin_right (GTK_WIDGET (back_forward), 12);

  /* Location and Reload/Stop */
  location_stop_reload = gtk_tool_item_new ();
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  /* Location */
  priv->entry = location = ephy_location_entry_new ();
  gtk_box_pack_start (GTK_BOX (box), location,
                      TRUE, TRUE, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (box),
                               "location-entry");

  /* Reload/Stop */
  tool_button = gtk_button_new ();
  /* FIXME: apparently we need an image inside the button for the action
   * icon to appear. */
  gtk_button_set_image (GTK_BUTTON (tool_button), gtk_image_new ());
  action = gtk_action_group_get_action (action_group, "ViewCombinedStopReload");
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (tool_button),
                                      action);
  gtk_container_add (GTK_CONTAINER (box), GTK_WIDGET (tool_button));

  gtk_container_add (GTK_CONTAINER (location_stop_reload), box);
  gtk_container_child_set (GTK_CONTAINER (toolbar),
                           GTK_WIDGET (location_stop_reload),
                           "expand", TRUE,
                           NULL);
  
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (location_stop_reload));

  gtk_size_group_add_widget (size, tool_button);
  gtk_size_group_add_widget (size, location);
  g_object_unref (size);

  gtk_widget_set_margin_right (GTK_WIDGET (location_stop_reload), 12);
  gtk_widget_show_all (GTK_WIDGET (location_stop_reload));

  /* Open in browser */
  tool_item = gtk_tool_item_new ();
  tool_button = gtk_button_new ();
  action_group = ephy_window_get_toolbar_action_group (priv->window);
  action = gtk_action_group_get_action (action_group, "NavigationOpenInBrowser");
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (tool_button),
                                      action);
  gtk_container_add (GTK_CONTAINER (tool_item), tool_button);
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (tool_item));
  gtk_widget_show_all (GTK_WIDGET (tool_item));

  /* Page Menu */
  tool_item = gtk_tool_item_new ();
  tool_button = gtk_button_new ();
  gtk_widget_set_name (GTK_WIDGET (tool_button), "ephy-page-menu-button");
  /* FIXME: apparently we need an image inside the button for the action
   * icon to appear. */
  gtk_button_set_image (GTK_BUTTON (tool_button), gtk_image_new ());
  action = gtk_action_group_get_action (action_group, "PageMenu");
  gtk_activatable_set_related_action (GTK_ACTIVATABLE (tool_button),
                                      action);
  gtk_container_add (GTK_CONTAINER (tool_item), tool_button);
  gtk_container_add (GTK_CONTAINER (toolbar), GTK_WIDGET (tool_item));
  gtk_widget_show_all (GTK_WIDGET (tool_item));
}

static void
ephy_toolbar_class_init (EphyToolbarClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ephy_toolbar_set_property;
    gobject_class->get_property = ephy_toolbar_get_property;
    gobject_class->constructed = ephy_toolbar_constructed;

    object_properties[PROP_WINDOW] =
      g_param_spec_object ("window",
                           "Window",
                           "The toolbar's EphyWindow",
                           EPHY_TYPE_WINDOW,
                           G_PARAM_READWRITE | G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (gobject_class,
                                       N_PROPERTIES,
                                       object_properties);

    g_type_class_add_private (klass, sizeof (EphyToolbarPrivate));
}

static void
ephy_toolbar_init (EphyToolbar *toolbar)
{
  toolbar->priv = EPHY_TOOLBAR_GET_PRIVATE (toolbar);
}

GtkWidget*
ephy_toolbar_new (EphyWindow *window)
{
    g_return_val_if_fail (EPHY_IS_WINDOW (window), NULL);

    return GTK_WIDGET (g_object_new (EPHY_TYPE_TOOLBAR,
                                     "window", window,
                                     NULL));
}

GtkWidget *
ephy_toolbar_get_location_entry (EphyToolbar *toolbar)
{
  return toolbar->priv->entry;
}
