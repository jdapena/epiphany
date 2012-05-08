/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
 *  Copyright © 2000-2003 Marco Pesenti Gritti
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

#define LIBSOUP_I_HAVE_READ_BUG_594377_AND_KNOW_SOUP_PASSWORD_MANAGER_MIGHT_GO_AWAY
#define LIBSOUP_USE_UNSTABLE_REQUEST_API
#define NSPLUGINWRAPPER_SETUP "/usr/bin/mozilla-plugin-config"

#include "ephy-embed-single.h"
#include "ephy-embed-shell.h"
#include "ephy-embed-prefs.h"
#include "ephy-embed-type-builtins.h"
#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "ephy-signal-accumulator.h"
#include "ephy-permission-manager.h"
#include "ephy-profile-utils.h"
#include "ephy-prefs.h"
#include "ephy-settings.h"
#include "ephy-request-about.h"
#include "ephy-request-chrome-extension.h"

#include <webkit/webkit.h>
#include <glib/gi18n.h>
#include <libsoup/soup-gnome.h>
#include <libsoup/soup-cache.h>
#include <libsoup/soup-requester.h>
#include <gnome-keyring.h>

#define EPHY_EMBED_SINGLE_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), EPHY_TYPE_EMBED_SINGLE, EphyEmbedSinglePrivate))

struct _EphyEmbedSinglePrivate {
  GHashTable *form_auth_data;
  SoupCache *cache;
};

static void ephy_embed_single_init (EphyEmbedSingle *single);
static void ephy_embed_single_class_init (EphyEmbedSingleClass *klass);
static void ephy_permission_manager_iface_init (EphyPermissionManagerIface *iface);

G_DEFINE_TYPE_WITH_CODE (EphyEmbedSingle, ephy_embed_single, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EPHY_TYPE_PERMISSION_MANAGER,
                                                ephy_permission_manager_iface_init))

static void
form_auth_data_free (EphyEmbedSingleFormAuthData *data)
{
  g_free (data->form_username);
  g_free (data->form_password);
  g_free (data->username);

  g_slice_free (EphyEmbedSingleFormAuthData, data);
}

static EphyEmbedSingleFormAuthData*
form_auth_data_new (const char *form_username,
                    const char *form_password,
                    const char *username)
{
  EphyEmbedSingleFormAuthData *data;

  data = g_slice_new (EphyEmbedSingleFormAuthData);
  data->form_username = g_strdup (form_username);
  data->form_password = g_strdup (form_password);
  data->username = g_strdup (username);

  return data;
}

static void
get_attr_cb (GnomeKeyringResult result,
             GnomeKeyringAttributeList *attributes,
             EphyEmbedSingle *single)
{
  int i = 0;
  GnomeKeyringAttribute *attribute;
  char *server = NULL, *username = NULL;

  if (result != GNOME_KEYRING_RESULT_OK)
    return;

  attribute = (GnomeKeyringAttribute*)attributes->data;
  for (i = 0; i < attributes->len; i++) {
    if (server && username)
      break;

    if (attribute[i].type == GNOME_KEYRING_ATTRIBUTE_TYPE_STRING) {
      if (g_str_equal (attribute[i].name, "server")) {
        server = g_strdup (attribute[i].value.string);
      } else if (g_str_equal (attribute[i].name, "user")) {
        username = g_strdup (attribute[i].value.string);
      }
    }
  }

  if (server && username &&
      g_strstr_len (server, -1, "form%5Fusername") &&
      g_strstr_len (server, -1, "form%5Fpassword")) {
    /* This is a stored login/password from a form, cache the form
     * names locally so we don't need to hit the keyring daemon all
     * the time */
    const char *form_username, *form_password;
    GHashTable *t;
    SoupURI *uri = soup_uri_new (server);
    t = soup_form_decode (uri->query);
    form_username = g_hash_table_lookup (t, FORM_USERNAME_KEY);
    form_password = g_hash_table_lookup (t, FORM_PASSWORD_KEY);
    ephy_embed_single_add_form_auth (single, uri->host, form_username, form_password, username);
    soup_uri_free (uri);
    g_hash_table_destroy (t);
  }

  g_free (server);
  g_free (username);
}

static void
store_form_data_cb (GnomeKeyringResult result, GList *l, EphyEmbedSingle *single)
{
  GList *p;

  if (result != GNOME_KEYRING_RESULT_OK)
    return;

  for (p = l; p; p = p->next) {
    guint key_id = GPOINTER_TO_UINT (p->data);
    gnome_keyring_item_get_attributes (GNOME_KEYRING_DEFAULT,
                                       key_id,
                                       (GnomeKeyringOperationGetAttributesCallback) get_attr_cb,
                                       single,
                                       NULL);
  }
}

static void
cache_keyring_form_data (EphyEmbedSingle *single)
{
  gnome_keyring_list_item_ids (GNOME_KEYRING_DEFAULT,
                               (GnomeKeyringOperationGetListCallback)store_form_data_cb,
                               single,
                               NULL);
}

static void
free_form_auth_data_list (gpointer data)
{
  GSList *p, *l = (GSList*)data;

  for (p = l; p; p = p->next)
    form_auth_data_free ((EphyEmbedSingleFormAuthData*)p->data);

  g_slist_free (l);
}

static void
remove_form_auth_data (gpointer key, gpointer value, gpointer user_data)
{
  if (value)
    free_form_auth_data_list ((GSList*)value);
}

static void
ephy_embed_single_dispose (GObject *object)
{
  EphyEmbedSinglePrivate *priv = EPHY_EMBED_SINGLE (object)->priv;

  if (priv->cache) {
    soup_cache_flush (priv->cache);
    soup_cache_dump (priv->cache);
    g_object_unref (priv->cache);
    priv->cache = NULL;
  }

  G_OBJECT_CLASS (ephy_embed_single_parent_class)->dispose (object);
}

static void
ephy_embed_single_finalize (GObject *object)
{
  EphyEmbedSinglePrivate *priv = EPHY_EMBED_SINGLE (object)->priv;

  if (priv->form_auth_data) {
    g_hash_table_foreach (priv->form_auth_data,
                          (GHFunc)remove_form_auth_data,
                          NULL);
    g_hash_table_destroy (priv->form_auth_data);
  }

  G_OBJECT_CLASS (ephy_embed_single_parent_class)->finalize (object);
}

static void
ephy_embed_single_init (EphyEmbedSingle *single)
{
  EphyEmbedSinglePrivate *priv;

  single->priv = priv = EPHY_EMBED_SINGLE_GET_PRIVATE (single);

  priv->form_auth_data = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);
  cache_keyring_form_data (single);
}

static void
ephy_embed_single_class_init (EphyEmbedSingleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ephy_embed_single_finalize;
  object_class->dispose = ephy_embed_single_dispose;

  /**
   * EphyEmbedSingle::new-window:
   * @parent_embed: the #EphyEmbed requesting the new window, or %NULL
   * @mask: a #EphyEmbedChrome
   *
   * The ::new_window signal is emitted when a new window needs to be opened.
   * For example, when a JavaScript popup window was opened.
   *
   * Returns: (transfer none): a new #EphyEmbed.
   **/
  g_signal_new ("new-window",
                EPHY_TYPE_EMBED_SINGLE,
                G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (EphyEmbedSingleClass, new_window),
                ephy_signal_accumulator_object, ephy_embed_get_type,
                g_cclosure_marshal_generic,
                GTK_TYPE_WIDGET,
                2,
                GTK_TYPE_WIDGET,
                EPHY_TYPE_WEB_VIEW_CHROME);

  /**
   * EphyEmbedSingle::handle_content:
   * @single:
   * @mime_type: the MIME type of the content
   * @address: the URL to the content
   *
   * The ::handle_content signal is emitted when encountering content of a mime
   * type Epiphany is unable to handle itself.
   *
   * If a connected callback returns %TRUE, the signal will stop propagating. For
   * example, this could be used by a download manager to prevent other
   * ::handle_content listeners from being called.
   **/
  g_signal_new ("handle_content",
                EPHY_TYPE_EMBED_SINGLE,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (EphyEmbedSingleClass, handle_content),
                g_signal_accumulator_true_handled, NULL,
                g_cclosure_marshal_generic,
                G_TYPE_BOOLEAN,
                2,
                G_TYPE_STRING,
                G_TYPE_STRING);

  g_type_class_add_private (object_class, sizeof (EphyEmbedSinglePrivate));
}

static void
impl_permission_manager_add (EphyPermissionManager *manager,
                             const char *host,
                             const char *type,
                             EphyPermission permission)
{
}

static void
impl_permission_manager_remove (EphyPermissionManager *manager,
                                const char *host,
                                const char *type)
{
}

static void
impl_permission_manager_clear (EphyPermissionManager *manager)
{
}

static EphyPermission
impl_permission_manager_test (EphyPermissionManager *manager,
                              const char *host,
                              const char *type)
{
  g_return_val_if_fail (type != NULL && type[0] != '\0', EPHY_PERMISSION_DEFAULT);

  return (EphyPermission)0;
}

static GList *
impl_permission_manager_list (EphyPermissionManager *manager,
                              const char *type)
{
  GList *list = NULL;
  return list;
}

static void
ephy_permission_manager_iface_init (EphyPermissionManagerIface *iface)
{
  iface->add = impl_permission_manager_add;
  iface->remove = impl_permission_manager_remove;
  iface->clear = impl_permission_manager_clear;
  iface->test = impl_permission_manager_test;
  iface->list = impl_permission_manager_list;
}

static void
cache_size_cb (GSettings *settings,
               char *key,
               EphyEmbedSingle *single)
{
  int new_cache_size = g_settings_get_int (settings, key);
  soup_cache_set_max_size (single->priv->cache, new_cache_size * 1024 * 1024 /* in bytes */);
}

/**
 * ephy_embed_single_initialize:
 * @single: the #EphyEmbedSingle
 * 
 * Performs startup initialisations. Must be called before calling
 * any other methods.
 **/
gboolean
ephy_embed_single_initialize (EphyEmbedSingle *single)
{
  SoupSession *session;
  SoupCookieJar *jar;
  char *filename;
  char *cookie_policy;
  char *cache_dir;
  char *favicon_db_path;
  EphyEmbedSinglePrivate *priv = single->priv;
  SoupSessionFeature *requester;

  /* Initialise nspluginwrapper's plugins if available */
  if (g_file_test (NSPLUGINWRAPPER_SETUP, G_FILE_TEST_EXISTS) != FALSE)
    g_spawn_command_line_sync (NSPLUGINWRAPPER_SETUP, NULL, NULL, NULL, NULL);

  session = webkit_get_default_session ();

  /* Check SSL certificates */
  g_object_set (session,
                SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
                SOUP_SESSION_SSL_STRICT, FALSE,
                NULL);

  /* Store cookies in moz-compatible SQLite format */
  filename = g_build_filename (ephy_dot_dir (), "cookies.sqlite", NULL);
  jar = soup_cookie_jar_sqlite_new (filename, FALSE);
  g_free (filename);
  cookie_policy = g_settings_get_string (EPHY_SETTINGS_WEB,
                                         EPHY_PREFS_WEB_COOKIES_POLICY);
  ephy_embed_prefs_set_cookie_jar_policy (jar, cookie_policy);
  g_free (cookie_policy);

  soup_session_add_feature (session, SOUP_SESSION_FEATURE (jar));
  g_object_unref (jar);

  /* Use GNOME proxy settings through libproxy */
  soup_session_add_feature_by_type (session, SOUP_TYPE_PROXY_RESOLVER_GNOME);

  /* WebKitSoupCache */
  cache_dir = g_build_filename (g_get_user_cache_dir (), g_get_prgname (), NULL);
  priv->cache = soup_cache_new (cache_dir, SOUP_CACHE_SINGLE_USER);
  g_free (cache_dir);

  soup_session_add_feature (session, SOUP_SESSION_FEATURE (priv->cache));
  /* Cache size in Mb: 1024 * 1024 */
  soup_cache_set_max_size (priv->cache, g_settings_get_int (EPHY_SETTINGS_WEB, EPHY_PREFS_CACHE_SIZE) << 20);
  soup_cache_load (priv->cache);

  g_signal_connect (EPHY_SETTINGS_WEB,
                    "changed::" EPHY_PREFS_CACHE_SIZE,
                    G_CALLBACK (cache_size_cb),
                    single);

  /* about: URIs handler */
  requester = SOUP_SESSION_FEATURE (soup_requester_new());
  soup_session_add_feature (session, requester);
  soup_session_feature_add_feature (requester, EPHY_TYPE_REQUEST_ABOUT);
  soup_session_feature_add_feature (requester, EPHY_TYPE_REQUEST_CHROME_EXTENSION);
  g_object_unref (requester);

#ifdef SOUP_TYPE_PASSWORD_MANAGER
  /* Use GNOME keyring to store passwords. Only add the manager if we
     are not using a private session, otherwise we want any new
     password to expire when we exit *and* we don't want to use any
     existing password in the keyring */
  if (ephy_embed_shell_get_mode (ephy_embed_shell_get_default ()) != EPHY_EMBED_SHELL_MODE_PRIVATE)
    soup_session_add_feature_by_type (session, SOUP_TYPE_PASSWORD_MANAGER_GNOME);
#endif

  /* Initialize the favicon cache. */
  favicon_db_path = g_build_filename (g_get_user_data_dir (), g_get_prgname (), NULL);
  webkit_favicon_database_set_path (webkit_get_favicon_database (), favicon_db_path);
  g_free (favicon_db_path);

  return TRUE;
}

/**
 * ephy_embed_single_clear_cache:
 * @single: the #EphyEmbedSingle
 * 
 * Clears the HTTP cache (temporarily saved web pages).
 **/
void
ephy_embed_single_clear_cache (EphyEmbedSingle *single)
{
  soup_cache_clear (single->priv->cache);
}

/**
 * ephy_embed_single_clear_auth_cache:
 * @single: the #EphyEmbedSingle
 * 
 * Clears the HTTP authentication cache.
 *
 * This does not clear regular website passwords; it only clears the HTTP
 * authentication cache. Websites which use HTTP authentication require the
 * browser to send a password along with every HTTP request; the browser will
 * ask the user for the password once and then cache the password for subsequent
 * HTTP requests. This function will clear the HTTP authentication cache,
 * meaning the user will have to re-enter a username and password the next time
 * Epiphany requests a web page secured with HTTP authentication.
 **/
void
ephy_embed_single_clear_auth_cache (EphyEmbedSingle *single)
{
}

/**
 * ephy_embed_single_open_window:
 * @single: the #EphyEmbedSingle
 * @parent: the requested window's parent #EphyEmbed
 * @address: the URL to load
 * @name: a name for the window
 * @features: a Javascript features string
 *
 * Opens a new window, as if it were opened in @parent using the Javascript
 * method and arguments: <code>window.open(&quot;@address&quot;,
 * &quot;_blank&quot;, &quot;@features&quot;);</code>.
 * 
 * Returns: (transfer none): the new embed. This is either a #EphyEmbed, or,
 * when @features specified "chrome", a #GtkMozEmbed.
 *
 * NOTE: Use ephy_shell_new_tab() unless this handling of the @features string
 * is required.
 */
GtkWidget *
ephy_embed_single_open_window (EphyEmbedSingle *single,
                               EphyEmbed *parent,
                               const char *address,
                               const char *name,
                               const char *features)
{
  return NULL;
}

/**
 * ephy_embed_single_get_form_auth:
 * @single: an #EphyEmbedSingle
 * @uri: the URI of a web page
 * 
 * Gets a #GSList of all stored login/passwords, in
 * #EphyEmbedSingleFormAuthData format, for any form in @uri, or %NULL
 * if we have none.
 * 
 * The #EphyEmbedSingleFormAuthData structs and the #GSList are owned
 * by @single and should not be freed by the user.
 * 
 * Returns: (transfer none) (element-type EphyEmbedSingleFormAuthData): #GSList with the possible auto-fills for the forms
 * in @uri, or %NULL
 **/
GSList *
ephy_embed_single_get_form_auth (EphyEmbedSingle *single,
                                 const char *uri)
{
  EphyEmbedSinglePrivate *priv;

  g_return_val_if_fail (EPHY_IS_EMBED_SINGLE (single), NULL);
  g_return_val_if_fail (uri, NULL);

  priv = single->priv;

  return g_hash_table_lookup (priv->form_auth_data, uri);
}

/**
 * ephy_embed_single_add_form_auth:
 * @single: an #EphyEmbedSingle
 * @uri: URI of the page
 * @form_username: name of the username input field
 * @form_password: name of the password input field
 * @username: username
 * 
 * Adds a new entry to the local cache of form auth data stored in
 * @single.
 * 
 **/
void
ephy_embed_single_add_form_auth (EphyEmbedSingle *single,
                                 const char *uri,
                                 const char *form_username,
                                 const char *form_password,
                                 const char *username)
{
  EphyEmbedSingleFormAuthData *form_data;
  EphyEmbedSinglePrivate *priv;
  GSList *l;

  g_return_if_fail (EPHY_IS_EMBED_SINGLE (single));
  g_return_if_fail (uri);
  g_return_if_fail (form_username);
  g_return_if_fail (form_password);
  g_return_if_fail (username);

  priv = single->priv;

  LOG ("Appending: name field: %s / pass field: %s / username: %s / uri: %s", form_username, form_password, username, uri);

  form_data = form_auth_data_new (form_username, form_password, username);
  l = g_hash_table_lookup (priv->form_auth_data,
                           uri);
  l = g_slist_append (l, form_data);
  g_hash_table_replace (priv->form_auth_data,
                        g_strdup (uri),
                        l);
}
