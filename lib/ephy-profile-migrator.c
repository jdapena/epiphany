/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* vim: set sw=2 ts=2 sts=2 et: */
/*
 *  Copyright © 2009 Xan López
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

/* Portions of this file based on Chromium code.
 * License block as follows:
 *
 * Copyright (c) 2009 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The LICENSE file from Chromium can be found in the LICENSE.chromium
 * file.
 */

#include "config.h"

#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "ephy-profile-utils.h"
#ifdef ENABLE_NSS
#include "ephy-nss-glue.h"
#endif

#include <glib/gi18n.h>
#include <gnome-keyring.h>
#include <libsoup/soup-gnome.h>

/*
 * What to do to add new migration steps:
 *  - Bump EPHY_PROFILE_MIGRATION_VERSION in lib/ephy-profile-utils.h
 *  - Add your function at the end of the 'migrators' array
 */

typedef void (*EphyProfileMigrator) (void);

static void
migrate_cookies ()
{
  const char *cookies_file_sqlite = "cookies.sqlite";
  const char *cookies_file_txt = "cookies.txt";
  char *src_sqlite = NULL, *src_txt = NULL, *dest = NULL;

  dest = g_build_filename (ephy_dot_dir (), cookies_file_sqlite, NULL);
  /* If we already have a cookies.sqlite file, do nothing */
  if (g_file_test (dest, G_FILE_TEST_EXISTS))
    goto out;

  src_sqlite = g_build_filename (ephy_dot_dir (), "mozilla",
                                 "epiphany", cookies_file_sqlite, NULL);
  src_txt = g_build_filename (ephy_dot_dir (), "mozilla",
                              "epiphany", cookies_file_txt, NULL);

  /* First check if we have a cookies.sqlite file in Mozilla */
  if (g_file_test (src_sqlite, G_FILE_TEST_EXISTS)) {
    GFile *gsrc, *gdest;

    /* Copy the file */
    gsrc = g_file_new_for_path (src_sqlite);
    gdest = g_file_new_for_path (dest);

    if (!g_file_copy (gsrc, gdest, 0, NULL, NULL, NULL, NULL))
      g_warning (_("Failed to copy cookies file from Mozilla."));

    g_object_unref (gsrc);
    g_object_unref (gdest);
  } else if (g_file_test (src_txt, G_FILE_TEST_EXISTS)) {
    /* Create a SoupCookieJarSQLite with the contents of the txt file */
    GSList *cookies, *p;
    SoupCookieJar *txt, *sqlite;

    txt = soup_cookie_jar_text_new (src_txt, TRUE);
    sqlite = soup_cookie_jar_sqlite_new (dest, FALSE);
    cookies = soup_cookie_jar_all_cookies (txt);

    for (p = cookies; p; p = p->next) {
      SoupCookie *cookie = (SoupCookie*)p->data;
      /* Cookie is stolen, so we won't free it */
      soup_cookie_jar_add_cookie (sqlite, cookie);
    }

    g_slist_free (cookies);
    g_object_unref (txt);
    g_object_unref (sqlite);
  }

 out:
  g_free (src_sqlite);
  g_free (src_txt);
  g_free (dest);
}

#ifdef ENABLE_NSS
static gchar*
_g_utf8_substr(const gchar* string, gint start, gint end)
{
  gchar *start_ptr, *output;
  gsize len_in_bytes;
  glong str_len = g_utf8_strlen (string, -1);

  if (start > str_len || end > str_len)
    return NULL;

  start_ptr = g_utf8_offset_to_pointer (string, start);
  len_in_bytes = g_utf8_offset_to_pointer (string, end) -  start_ptr + 1;
  output = g_malloc0 (len_in_bytes + 1);

  return g_utf8_strncpy (output, start_ptr, end - start + 1);
}

static char*
decrypt (const char *data)
{
  unsigned char *plain;
  gsize out_len;

  /* The old style password is encoded in base64. They are identified
   * by a leading '~'. Otherwise, we should decrypt the text.
   */
  plain = g_base64_decode (data, &out_len);
  if (data[0] != '~') {
    char *decrypted;

    decrypted = ephy_nss_glue_decrypt (plain, out_len);
    g_free (plain);
    plain = (unsigned char*)decrypted;
  }

  return (char*)plain;
}

static void
parse_and_decrypt_signons (const char *signons,
                           gboolean handle_forms)
{
  int version;
  gchar **lines;
  int i;
  guint length;

  lines = g_strsplit (signons, "\r\n", -1);
  if (!g_ascii_strncasecmp (lines[0], "#2c", 3))
    version = 1;
  else if (!g_ascii_strncasecmp (lines[0], "#2d", 3))
    version = 2;
  else if (!g_ascii_strncasecmp (lines[0], "#2e", 3))
    version = 3;
  else
    goto out;

  /* Skip the never-saved list */
  for (i = 1; lines[i] && !g_str_equal (lines[i], "."); i++) {
    ;
  }

  i++;

  /*
   * Read saved passwords. The information is stored in blocks
   * separated by lines that only contain a dot. We find a block by
   * the separator and parse them one by one.
   */
  length = g_strv_length (lines);

  while (i < length) {
    size_t begin = i;
    size_t end = i + 1;
    const char *realmBracketBegin = " (";
    const char *realmBracketEnd = ")";
    SoupURI *uri = NULL;
    char *realm = NULL;

    while (lines[end] && !g_str_equal (lines[end], "."))
      end++;

    i = end + 1;

    /* A block has at least five lines */
    if (end - begin < 5)
      continue;

    /* The first line is the site URL.
     * For HTTP authentication logins, the URL may contain http realm,
     * which will be in bracket:
     *   sitename:8080 (realm)
     */
    if (g_strstr_len (lines[begin], -1, realmBracketBegin)) {
      char *start_ptr, *end_ptr;
      char *full_url, *url;
      glong start, end;

      /* In this case the scheme may not exist. We assume that the
       * scheme is HTTP.
       */
      if (!g_strstr_len (lines[begin], -1, "://"))
        full_url = g_strconcat ("http://", lines[begin], NULL);
      else
        full_url = g_strdup (lines[begin]);

      start_ptr = g_strstr_len (full_url, -1, realmBracketBegin);
      start = g_utf8_pointer_to_offset (full_url, start_ptr);
      url = _g_utf8_substr (full_url, 0, start);
      url = g_strstrip (url);
      uri = soup_uri_new (url);
      g_free (url);

      start += strlen (realmBracketBegin);
      end_ptr = g_strstr_len (full_url, -1, realmBracketEnd) -1;
      end = g_utf8_pointer_to_offset (full_url, end_ptr);
      realm = _g_utf8_substr (full_url, start, end);

      g_free (full_url);
    } else {
      /* Don't have HTTP realm. It is the URL that the following
       * password belongs to.
       */
      uri = soup_uri_new (lines[begin]);
    }

    if (!SOUP_URI_VALID_FOR_HTTP (uri)) {
      soup_uri_free (uri);
      g_free (realm);
      continue;
    }

    ++begin;

    /* There may be multiple username/password pairs for this site.
     * In this case, they are saved in one block without a separated
     * line (contains a dot).
     */
    while (begin + 4 < end) {
      char *username = NULL;
      char *password = NULL;
      char *form_username = NULL;
      char *form_password = NULL;
      guint32 item_id;

      /* The username */
      if (handle_forms) {
        form_username = g_strdup (lines[begin++]);
      } else {
        begin++; /* Skip username element */
      }
      username = decrypt (lines[begin++]);

      /* The password */
      /* The element name has a leading '*' */
      if (lines[begin][0] == '*') {
        if (handle_forms) {
          form_password = g_strdup (lines[begin++]);
        } else {
          begin++; /* Skip password element */
        }
        password = decrypt (lines[begin++]);
      } else {
        /* Maybe the file is broken, skip to the next block */
        g_free (username);
        break;
      }

      /* The action attribute for from the form element. This line
       * exists in version 2 or above
       */
      if (version >= 2) {
        if (begin < end)
          /* Skip it */ ;
        begin++;

        /* Version 3 has an extra line for further use */
        if (version == 3)
          begin++;
      }

      if (handle_forms && !realm &&
          username && password &&
          !g_str_equal (username, "") &&
          !g_str_equal (form_username, "") &&
          !g_str_equal (form_password, "*")) {
        char *u = soup_uri_to_string (uri, FALSE);
        /* We skip the '*' at the beginning of form_password. */
        _ephy_profile_utils_store_form_auth_data (u,
                                                 form_username,
                                                 form_password+1,
                                                 username,
                                                 password);
        g_free (u);
      } else if (!handle_forms && realm &&
                 username && password &&
                 !g_str_equal (username, "") &&
                 form_username == NULL && form_password == NULL) {
        gnome_keyring_set_network_password_sync (NULL,
                                                 username,
                                                 realm,
                                                 uri->host,
                                                 NULL,
                                                 uri->scheme,
                                                 NULL,
                                                 uri->port,
                                                 password,
                                                 &item_id);
      }

      g_free (username);
      g_free (password);
      g_free (form_username);
      g_free (form_password);
    }

    soup_uri_free (uri);
    g_free (realm);
  }

 out:
  g_strfreev (lines);
}
#endif

static void
migrate_passwords ()
{
#ifdef ENABLE_NSS
  char *dest, *contents, *gecko_passwords_backup;
  gsize length;
  GError *error = NULL;

  dest = g_build_filename (ephy_dot_dir (),
                           "mozilla", "epiphany", "signons3.txt",
                           NULL);
  if (!g_file_test (dest, G_FILE_TEST_EXISTS)) {
    g_free (dest);
    dest = g_build_filename (ephy_dot_dir (),
                             "mozilla", "epiphany", "signons2.txt",
                             NULL);
    if (!g_file_test (dest, G_FILE_TEST_EXISTS)) {
      g_free (dest);
      return;
    }
  }

  if (!ephy_nss_glue_init ())
    return;

  if (!g_file_get_contents (dest, &contents, &length, &error)) {
    g_free (dest);
  }

  parse_and_decrypt_signons (contents, FALSE);

  /* Save the contents in a backup directory for future data
     extraction when we support more features */
  gecko_passwords_backup = g_build_filename (ephy_dot_dir (),
                                             "gecko-passwords.txt", NULL);

  if (!g_file_set_contents (gecko_passwords_backup, contents,
                            -1, &error)) {
    g_error_free (error);
  }

  g_free (gecko_passwords_backup);
  g_free (contents);

  ephy_nss_glue_close ();
#endif
}

static void
migrate_passwords2 ()
{
#ifdef ENABLE_NSS
  char *dest, *contents;
  gsize length;
  GError *error = NULL;

  dest = g_build_filename (ephy_dot_dir (),
                           "gecko-passwords.txt",
                           NULL);
  if (!g_file_test (dest, G_FILE_TEST_EXISTS)) {
    g_free (dest);
    return;
  }

  if (!ephy_nss_glue_init ())
    return;

  if (!g_file_get_contents (dest, &contents, &length, &error)) {
    g_free (dest);
  }

  parse_and_decrypt_signons (contents, TRUE);
  g_free (contents);

  ephy_nss_glue_close ();
#endif
}

const EphyProfileMigrator migrators[] = {
  migrate_cookies,
  migrate_passwords,
 /* Yes, again! Version 2 had some bugs, so we need to run
    migrate_passwords again to possibly migrate more passwords*/
  migrate_passwords,
  /* Very similar to migrate_passwords, but this migrates
   * login/passwords for page forms, which we previously ignored */
  migrate_passwords2
};

static void
ephy_migrator ()
{
  int latest, i;

  latest = ephy_profile_utils_get_migration_version ();

  LOG ("Running migrators up to version %d, current migration version is %d.",
       EPHY_PROFILE_MIGRATION_VERSION, latest);

  for (i = latest; i < EPHY_PROFILE_MIGRATION_VERSION; i++) {
    EphyProfileMigrator m;

    LOG ("Running migrator: %d of %d", i, EPHY_PROFILE_MIGRATION_VERSION);

    /* No need to run the password migration twice in a row. It
       appears twice in the list for the benefit of people that were
       using the development snapshots, since an early version didn't
       migrate all passwords correctly. */
    if (i == 1)
      continue;

    m = migrators[i];
    m();
  }

  if (ephy_profile_utils_set_migration_version (EPHY_PROFILE_MIGRATION_VERSION) != TRUE)
    LOG ("Failed to store the current migration version");
}

int
main (int argc, char *argv[])
{
  g_thread_init (NULL);

  ephy_debug_init ();

  if (!ephy_file_helpers_init (NULL, FALSE, FALSE, NULL)) {
    LOG ("Something wrong happened with ephy_file_helpers_init()");
    return -1;
  }

  ephy_migrator ();

  return 0;
}