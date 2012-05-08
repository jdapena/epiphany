/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "ephy-web-app.h"


G_DEFINE_TYPE (EphyWebApp, ephy_web_app, G_TYPE_OBJECT)

struct _EphyWebAppPrivate {
  GKeyFile *key_file;
  gchar *profile_dir;
};

static void
ephy_web_app_init (EphyWebApp *app)
{
  app->priv->key_file = g_key_file_new ();
}

static void
ephy_web_app_dispose (GObject *obj)
{
  EphyWebAppPrivate *priv = EPHY_WEB_APP (obj)->priv;

  g_key_file_free (priv->key_file);

  G_OBJECT_CLASS (ephy_web_app_parent_class)->dispose (obj);
}

static void
ephy_web_app_class_init (EphyWebAppClass *web_app_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (web_app_class);

  gobject_class->dispose = ephy_web_app_dispose;

  g_type_class_add_private (web_app_class, sizeof (EphyWebAppPrivate));
}
