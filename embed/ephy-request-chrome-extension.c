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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "ephy-request-chrome-extension.h"

#include "ephy-embed-shell.h"
#include "ephy-file-helpers.h"
#include "ephy-smaps.h"
#include "ephy-js-chrome-apps.h"
#include "ephy-web-application.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <libsoup/soup-uri.h>
#include <webkit/webkit.h>

G_DEFINE_TYPE (EphyRequestChromeExtension, ephy_request_chrome_extension, SOUP_TYPE_REQUEST)

struct _EphyRequestChromeExtensionPrivate {
  goffset size;
  char *mime_type;
};

static void
ephy_request_chrome_extension_init (EphyRequestChromeExtension *extension)
{
  extension->priv = G_TYPE_INSTANCE_GET_PRIVATE (extension, EPHY_TYPE_REQUEST_CHROME_EXTENSION, EphyRequestChromeExtensionPrivate);
  extension->priv->size = -1;
}

static void
ephy_request_chrome_extension_finalize (GObject *obj)
{
  EphyRequestChromeExtensionPrivate *priv = EPHY_REQUEST_CHROME_EXTENSION (obj)->priv;

  g_free (priv->mime_type);

  G_OBJECT_CLASS (ephy_request_chrome_extension_parent_class)->finalize (obj);
}

static gboolean
ephy_request_chrome_extension_check_uri (SoupRequest  *request,
					 SoupURI      *uri,
					 GError      **error)
{
  EphyWebApplication *app;
  const char *id;
  gboolean result = FALSE;

  app = ephy_embed_shell_get_application (ephy_embed_shell_get_default ());
  if (!app)
    return FALSE;

  id = ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID);
  if (g_strcmp0 (id, soup_uri_get_host (uri)) == 0)
    result = TRUE;
		
  return result;
}

static GInputStream *
ephy_request_chrome_extension_send (SoupRequest          *request,
				    GCancellable         *cancellable,
				    GError              **error)
{
  EphyRequestChromeExtension *extension = EPHY_REQUEST_CHROME_EXTENSION (request);
  SoupURI *uri = soup_request_get_uri (request);
  GInputStream *stream;
  GError *my_error = NULL;
  char *real_path_str;
  GFile *gfile;
  EphyWebApplication *app;

  app = ephy_embed_shell_get_application (ephy_embed_shell_get_default ());
  if (!app)
    return NULL;

  real_path_str = g_build_filename
    (ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS),
     soup_uri_get_path (uri), NULL);

  gfile = g_file_new_for_path (real_path_str);
  g_free (real_path_str);

  if (gfile == NULL)
    return NULL;

  stream = G_INPUT_STREAM (g_file_read (gfile,
					cancellable, &my_error));
  if (stream == NULL) {
    g_propagate_error (error, my_error);
  } else {
    GFileInfo *info = g_file_query_info (gfile,
					 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
					 G_FILE_ATTRIBUTE_STANDARD_SIZE,
					 0, cancellable, NULL);
    if (info) {
      const char *content_type;
      extension->priv->size = g_file_info_get_size (info);
      content_type = g_file_info_get_content_type (info);

      if (content_type)
	extension->priv->mime_type = g_content_type_get_mime_type (content_type);
      g_object_unref (info);
    }
  }

  return stream;
}

static goffset
ephy_request_chrome_extension_get_content_length (SoupRequest *request)
{
  EphyRequestChromeExtension *extension = EPHY_REQUEST_CHROME_EXTENSION (request);

  return extension->priv->size;
}

static const char *
ephy_request_chrome_extension_get_content_type (SoupRequest *request)
{
  EphyRequestChromeExtension *extension = EPHY_REQUEST_CHROME_EXTENSION (request);

  if (extension->priv->mime_type == NULL)
    return "application/octet-stream";

  return extension->priv->mime_type;
}

static const char *extension_schemes[] = { EPHY_CHROME_EXTENSION_SCHEME, NULL };

static void
ephy_request_chrome_extension_class_init (EphyRequestChromeExtensionClass *request_chrome_extension_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (request_chrome_extension_class);
  SoupRequestClass *request_class = SOUP_REQUEST_CLASS (request_chrome_extension_class);

  gobject_class->finalize = ephy_request_chrome_extension_finalize;

  request_class->schemes = extension_schemes;
  request_class->check_uri = ephy_request_chrome_extension_check_uri;
  request_class->send = ephy_request_chrome_extension_send;
  request_class->get_content_length = ephy_request_chrome_extension_get_content_length;
  request_class->get_content_type = ephy_request_chrome_extension_get_content_type;

  g_type_class_add_private (request_chrome_extension_class, sizeof (EphyRequestChromeExtensionPrivate));
}
