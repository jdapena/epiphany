/*
 *  Copyright © 2006 Christian Persch
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

#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "ephy-debug.h"
#include "ephy-string.h"

#include "ephy-print-utils.h"

#define PRINT_SETTINGS_GROUP	"Print Settings"
#define PAGE_SETUP_GROUP	"Page Setup"
#define PAPER_SIZE_GROUP	"Paper Size"

#define ERROR_QUARK		(g_quark_from_static_string ("ephy-print-utils-error"))

/**
 * ephy_print_utils_page_setup_new_from_file:
 * @file_name: the filename to read the page_setup from
 * @error:
 * 
 * Reads the print page_setup from @filename. Returns a new #GtkPageSetup
 * object with the restored page_setup, or %NULL if an error occurred.
 *
 * Return value: the restored #GtkPageSetup
 * 
 * Since: 2.10
 */
GtkPageSetup *
ephy_print_utils_page_setup_new_from_file (const gchar *file_name,
					   GError     **error)
{
  GtkPageSetup *page_setup;
  GKeyFile *key_file;

  g_return_val_if_fail (file_name != NULL, NULL);

  key_file = g_key_file_new ();
  if (!g_key_file_load_from_file (key_file, file_name, 0, error))
    {
      g_key_file_free (key_file);
      return NULL;
    }

  page_setup = ephy_print_utils_page_setup_new_from_key_file (key_file, error);
  g_key_file_free (key_file);

  return page_setup;
}

/**
 * ephy_print_utils_page_setup_new_from_key_file:
 * @key_file: the #GKeyFile to retrieve the page_setup from
 * @error:
 * 
 * Reads the print page_setup from @key_file. Returns a new #GtkPageSetup
 * object with the restored page_setup, or %NULL if an error occurred.
 *
 * Return value: the restored #GtkPageSetup
 * 
 * Since: 2.10
 */
GtkPageSetup *
ephy_print_utils_page_setup_new_from_key_file (GKeyFile *key_file,
					       GError  **error)
{
  GtkPageSetup *page_setup = NULL;
  GtkPaperSize *paper_size = NULL;
  gdouble width, height, top, bottom, left, right;
  char *name = NULL, *ppd_name = NULL, *display_name = NULL, *orientation = NULL;
  GError *err = NULL;

  g_return_val_if_fail (key_file != NULL, NULL);

  if (!g_key_file_has_group (key_file, PAGE_SETUP_GROUP) ||
      !g_key_file_has_group (key_file, PAPER_SIZE_GROUP)) {
    g_set_error (error, ERROR_QUARK, 0, "Not a valid epiphany page setup file");
    goto out;
  }

#define GET_DOUBLE(kf, group, name, v) \
v = g_key_file_get_double (kf, group, name, &err); \
if (err != NULL) {\
  g_propagate_error (error, err);\
  goto out;\
}

  GET_DOUBLE (key_file, PAPER_SIZE_GROUP, "Width", width);
  GET_DOUBLE (key_file, PAPER_SIZE_GROUP, "Height", height);
  GET_DOUBLE (key_file, PAGE_SETUP_GROUP, "MarginTop", top);
  GET_DOUBLE (key_file, PAGE_SETUP_GROUP, "MarginBottom", bottom);
  GET_DOUBLE (key_file, PAGE_SETUP_GROUP, "MarginLeft", left);
  GET_DOUBLE (key_file, PAGE_SETUP_GROUP, "MarginRight", right);

#undef GET_DOUBLE

  name = g_key_file_get_string (key_file, PAPER_SIZE_GROUP,
				"Name", NULL);
  ppd_name = g_key_file_get_string (key_file, PAPER_SIZE_GROUP,
				    "PPDName", NULL);
  display_name = g_key_file_get_string (key_file, PAPER_SIZE_GROUP,
					"DisplayName", NULL);
  orientation = g_key_file_get_string (key_file, PAGE_SETUP_GROUP,
				       "Orientation", NULL);

  if ((ppd_name == NULL && name == NULL) || orientation == NULL)
    {
      g_set_error (error, ERROR_QUARK, 0, "Not a valid epiphany page setup file");
      goto out;
    }

  if (ppd_name != NULL) {
    paper_size = gtk_paper_size_new_from_ppd (ppd_name, display_name,
					      width, height);
  } else {
    paper_size = gtk_paper_size_new_custom (name, display_name,
					    width, height, GTK_UNIT_MM);
  }
  g_assert (paper_size != NULL);

  page_setup = gtk_page_setup_new ();
  gtk_page_setup_set_paper_size (page_setup, paper_size);
  gtk_paper_size_free (paper_size);

  gtk_page_setup_set_top_margin (page_setup, top, GTK_UNIT_MM);
  gtk_page_setup_set_bottom_margin (page_setup, bottom, GTK_UNIT_MM);
  gtk_page_setup_set_left_margin (page_setup, left, GTK_UNIT_MM);
  gtk_page_setup_set_right_margin (page_setup, right, GTK_UNIT_MM);

  gtk_page_setup_set_orientation (page_setup,
				  ephy_string_enum_from_string (GTK_TYPE_PAGE_ORIENTATION,
						    		orientation));
out:
  g_free (ppd_name);
  g_free (name);
  g_free (display_name);
  g_free (orientation);

  return page_setup;
}

/**
 * ephy_print_utils_page_setup_to_file:
 * @page_setup: a #GtkPageSetup
 * @file_name: the file to save to
 * @error:
 * 
 * This function saves the print page_setup from @page_setup to @file_name.
 * 
 * Return value: %TRUE on success
 *
 * Since: 2.10
 */
gboolean
ephy_print_utils_page_setup_to_file (GtkPageSetup     *page_setup,
				     const char           *file_name,
				     GError              **error)
{
  GKeyFile *keyfile;
  gboolean retval;
  char *data = NULL;
  gsize len;

  g_return_val_if_fail (GTK_IS_PAGE_SETUP (page_setup), FALSE);
  g_return_val_if_fail (file_name != NULL, FALSE);

  keyfile = g_key_file_new ();
  retval = ephy_print_utils_page_setup_to_key_file (page_setup, keyfile, error);
  if (!retval) goto out;

  data = g_key_file_to_data (keyfile, &len, error);
  if (!data) goto out;

  retval = g_file_set_contents (file_name, data, len, error);

out:
  g_key_file_free (keyfile);
  g_free (data);

  return retval;
}

/**
 * ephy_print_utils_page_setup_to_key_file:
 * @page_setup: a #GtkPageSetup
 * @key_file: the #GKeyFile to save the print page_setup to
 * @error:
 * 
 * This function adds the print page_setup from @page_setup to @key_file.
 * 
 * Return value: %TRUE on success
 *
 * Since: 2.10
 */
gboolean
ephy_print_utils_page_setup_to_key_file (GtkPageSetup  *page_setup,
					 GKeyFile          *key_file,
					 GError           **error)
{
  GtkPaperSize *paper_size;
  const char *name, *ppd_name, *display_name;
  char *orientation;

  g_return_val_if_fail (GTK_IS_PAGE_SETUP (page_setup), FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);

  paper_size = gtk_page_setup_get_paper_size (page_setup);
  g_assert (paper_size != NULL);

  name = gtk_paper_size_get_name (paper_size);
  display_name = gtk_paper_size_get_display_name (paper_size);
  ppd_name = gtk_paper_size_get_ppd_name (paper_size);

  if (ppd_name != NULL) {
    g_key_file_set_string (key_file, PAPER_SIZE_GROUP,
			   "PPDName", ppd_name);
  } else {
    g_key_file_set_string (key_file, PAPER_SIZE_GROUP,
			   "Name", name);
  }

  if (display_name) {
    g_key_file_set_string (key_file, PAPER_SIZE_GROUP,
			   "DisplayName", display_name);
  }

  g_key_file_set_double (key_file, PAPER_SIZE_GROUP,
			 "Width", gtk_paper_size_get_width (paper_size, GTK_UNIT_MM));
  g_key_file_set_double (key_file, PAPER_SIZE_GROUP,
			 "Height", gtk_paper_size_get_height (paper_size, GTK_UNIT_MM));

  g_key_file_set_double (key_file, PAGE_SETUP_GROUP,
			 "MarginTop", gtk_page_setup_get_top_margin (page_setup, GTK_UNIT_MM));
  g_key_file_set_double (key_file, PAGE_SETUP_GROUP,
			 "MarginBottom", gtk_page_setup_get_bottom_margin (page_setup, GTK_UNIT_MM));
  g_key_file_set_double (key_file, PAGE_SETUP_GROUP,
			 "MarginLeft", gtk_page_setup_get_left_margin (page_setup, GTK_UNIT_MM));
  g_key_file_set_double (key_file, PAGE_SETUP_GROUP,
			 "MarginRight", gtk_page_setup_get_right_margin (page_setup, GTK_UNIT_MM));

  orientation = ephy_string_enum_to_string (GTK_TYPE_PAGE_ORIENTATION,
					    gtk_page_setup_get_orientation (page_setup));
  g_key_file_set_string (key_file, PAGE_SETUP_GROUP,
			 "Orientation", orientation);
  g_free (orientation);

  return TRUE;
}
