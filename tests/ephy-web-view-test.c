/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-web-view-test.c
 * This file is part of Epiphany
 *
 * Copyright © 2012 - Igalia S.L.
 *
 * Epiphany is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Epiphany is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Epiphany; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "ephy-debug.h"
#include "ephy-embed-prefs.h"
#include "ephy-embed-private.h"
#include "ephy-file-helpers.h"
#include "ephy-private.h"
#include "ephy-shell.h"
#include "ephy-web-view.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <string.h>

#define HTML_STRING "testing-ephy-web-view"
#define SERVER_PORT 12321

static void
server_callback (SoupServer *server,
                 SoupMessage *msg,
                 const char *path,
                 GHashTable *query,
                 SoupClientContext *context,
                 gpointer data)
{
  if (g_str_equal (path, "/cancelled"))
    soup_message_set_status (msg, SOUP_STATUS_CANT_CONNECT);
  else if (g_str_equal (path, "/redirect")) {
    soup_message_set_status (msg, SOUP_STATUS_MOVED_PERMANENTLY);
    soup_message_headers_append (msg->response_headers, "Location", "/redirect-result");
  } else
    soup_message_set_status (msg, SOUP_STATUS_OK);


  soup_message_body_append (msg->response_body, SOUP_MEMORY_STATIC,
                            HTML_STRING, strlen (HTML_STRING));

  soup_message_body_complete (msg->response_body);
}

#ifdef HAVE_WEBKIT2
static void
load_changed_cb (WebKitWebView *view, WebKitLoadEvent load_event, GMainLoop *loop)
{
  char *expected_url;
  const char *loaded_url;

  if (load_event != WEBKIT_LOAD_FINISHED)
    return;

  expected_url = g_object_get_data (G_OBJECT (view), "test.expected_url");
  g_assert (expected_url != NULL);

  loaded_url = webkit_web_view_get_uri (view);
  g_assert_cmpstr (loaded_url, ==, expected_url);

  g_signal_handlers_disconnect_by_func (view, load_changed_cb, loop);

  g_free (expected_url);
  g_main_loop_quit (loop);
}
#else
static void
notify_load_status_cb (WebKitWebView *view, GParamSpec *spec, GMainLoop *loop)
{
  WebKitLoadStatus status;
  char *expected_url;
  const char *loaded_url;

  status = webkit_web_view_get_load_status (view);

  if (status != WEBKIT_LOAD_FINISHED)
    return;

  expected_url = g_object_get_data (G_OBJECT (view), "test.expected_url");
  g_assert (expected_url != NULL);

  loaded_url = webkit_web_view_get_uri (view);
  g_assert_cmpstr (loaded_url, ==, expected_url);

  g_signal_handlers_disconnect_by_func (view, notify_load_status_cb, loop);

  g_free (expected_url);
  g_main_loop_quit (loop);
}
#endif

typedef struct {
  const char *url;
  const char *expected_url;
} URLTest;

static const URLTest test_load_url[] = {
  /* This will load the server unavailable error page unless you have a
   * local server in port 80 */
  { "localhost", "http://localhost/" },
  { "127.0.0.1", "http://127.0.0.1/" },

  /* Require internet */

  { "127.0.0.1:12321",
    "http://127.0.0.1:12321/" },
  { "127.0.0.1:12321/path",
    "http://127.0.0.1:12321/path" },

  /* port is SERVER_PORT */
  { "localhost:12321",
    "http://localhost:12321/" },

#if 0
  /* FAIL */
  { "gnome.org:80",
    "http://www.gnome.org/" },
#endif

  /* Queries */
  { "localhost:12321/?key=value",
    "http://localhost:12321/?key=value" },
  { "localhost:12321/?key=value:sub-value",
    "http://localhost:12321/?key=value:sub-value" },
  { "localhost:12321/?key=value&key2=value2",
    "http://localhost:12321/?key=value&key2=value2" },
  { "localhost:12321/?key=value&key2=",
    "http://localhost:12321/?key=value&key2=" },

  /* Other HTTP status */
  { "localhost:12321/redirect",
    "http://localhost:12321/redirect-result" },

  { "about:epiphany", "ephy-about:epiphany" },
  { "about:applications", "ephy-about:applications" },
  { "about:memory", "ephy-about:memory" },
  { "about:plugins", "ephy-about:plugins" },
};

/* Tests that EphyWebView is successfully loading the given URL. */
static void
test_ephy_web_view_load_url ()
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (test_load_url); i++) {
    URLTest test;
    GMainLoop *loop;
    EphyWebView *view;

    view = EPHY_WEB_VIEW (ephy_web_view_new ());
    test = test_load_url[i];
    loop = g_main_loop_new (NULL, FALSE);

    ephy_web_view_load_url (view, test.url);

    g_object_set_data (G_OBJECT (view),
                       "test.expected_url", g_strdup (test.expected_url));

    g_test_message ("[%s] \t-> %s", test.url, test.expected_url);

#ifdef HAVE_WEBKIT2
    g_signal_connect (view, "load-changed",
                      G_CALLBACK (load_changed_cb), loop);
#else
    g_signal_connect (view, "notify::load-status",
                      G_CALLBACK (notify_load_status_cb), loop);
#endif

    g_main_loop_run (loop);
    g_main_loop_unref (loop);
    g_object_unref (g_object_ref_sink (view));
  }
}

typedef struct {
  const char *url;
  gboolean match;
} RegexTest;

/* Formal, correct, URLs should be match=TRUE */
static const RegexTest test_non_search_regex[] = {
  /* Searches */
  { "localhost localdomain:8080/home/", FALSE },

  /* Relative paths should be searched */
  { "./", FALSE },

  { "localhost", TRUE },
  { "localhost.localdomain", TRUE },
  { "localhost.localdomain:8080", TRUE },
  { "localhost.localdomain:8080/home/", TRUE },

  { "gnome.org", TRUE },
  { "www.gnome.org", TRUE },
  { "http://www.gnome.org", TRUE },

  /* Ip */
  { "192.168.1.1", TRUE },
  { "192.168.1.1:80", TRUE },
  { "f80e::2a1:f85f:fc8f:a0d1", TRUE },

#if 0
  /* FAIL */
  { "192.168.1.1:80 fails to load", FALSE },
  { "localhost.localdomain 8080/home/", FALSE },
  { "org.gnome.Epiphany: failed to start", FALSE },
  { "ephy-web-view.c:130 error something", FALSE },

  { "[f80e::2a1:f85f:fc8f:a0d1]:80", TRUE },
#endif
};

static void
test_ephy_web_view_non_search_regex ()
{
  GRegex *regex;
  GError *error = NULL;
  int i;

  regex = g_regex_new (EPHY_WEB_VIEW_NON_SEARCH_REGEX,
                       0, G_REGEX_MATCH_NOTEMPTY, &error);

  if (error) {
    g_test_message ("Regex failed: %s", error->message);
    g_error_free (error);
  }

  g_assert (regex != NULL);

  for (i = 0; i < G_N_ELEMENTS (test_non_search_regex); i++) {
    RegexTest test;

    test = test_non_search_regex[i];

    g_test_message ("%s\t\t%s",
                    test.match ? "NO SEARCH" : "SEARCH",
                    test.url);

    g_assert (g_regex_match (regex, test.url, 0, NULL) == test.match);
  }

  g_regex_unref (regex);
}

int
main (int argc, char *argv[])
{
  int ret;
  SoupServer *server;

  gtk_test_init (&argc, &argv);

  ephy_debug_init ();
  ephy_embed_prefs_init ();

  if (!ephy_file_helpers_init (NULL,
                               EPHY_FILE_HELPERS_PRIVATE_PROFILE | EPHY_FILE_HELPERS_ENSURE_EXISTS,
                               NULL)) {
    g_debug ("Something wrong happened with ephy_file_helpers_init()");
    return -1;
  }

  _ephy_shell_create_instance (EPHY_EMBED_SHELL_MODE_PRIVATE, NULL);

  server = soup_server_new (SOUP_SERVER_PORT, SERVER_PORT, NULL);
  soup_server_add_handler (server, NULL, server_callback, NULL, NULL);
  soup_server_run_async (server);

  g_test_add_func ("/embed/ephy-web-view/non_search_regex",
                   test_ephy_web_view_non_search_regex);

  g_test_add_func ("/embed/ephy-web-view/load_url",
                   test_ephy_web_view_load_url);

  ret = g_test_run ();

  g_object_unref (server);
  g_object_unref (ephy_shell);
  ephy_file_helpers_shutdown ();

  return ret;
}
