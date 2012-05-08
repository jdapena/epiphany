/*
 *  Copyright © 2002 Jorn Baayen
 *  Copyright © 2003, 2004 Marco Pesenti Gritti
 *  Copyright © 2004, 2005, 2006 Christian Persch
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

#ifndef EPHY_FILE_HELPERS_H
#define EPHY_FILE_HELPERS_H

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

extern GQuark ephy_file_helpers_error_quark;
#define EPHY_FILE_HELPERS_ERROR_QUARK	(ephy_file_helpers_error_quark)

G_BEGIN_DECLS

typedef enum
{
	EPHY_MIME_PERMISSION_SAFE	= 1,
	EPHY_MIME_PERMISSION_UNSAFE	= 2,
	EPHY_MIME_PERMISSION_UNKNOWN	= 3
} EphyMimePermission;

typedef enum
{
	EPHY_FILE_HELPERS_NONE		   = 0,
	EPHY_FILE_HELPERS_KEEP_TEMP_DIR	   = 1 << 1,
	EPHY_FILE_HELPERS_PRIVATE_PROFILE  = 1 << 2,
	EPHY_FILE_HELPERS_ENSURE_EXISTS	   = 1 << 3,
} EphyFileHelpersFlags;

#define EPHY_UUID_ENVVAR	"EPHY_UNIQUE"

gboolean           ephy_file_helpers_init        (const char  *profile_dir,
                                                  EphyFileHelpersFlags flags,
                                                  GError     **error);
const char *       ephy_file                     (const char  *filename);
const char *       ephy_dot_dir                  (void);
void               ephy_file_helpers_shutdown    (void);
char	   *       ephy_file_get_downloads_dir   (void);
char       *       ephy_file_desktop_dir         (void);
const char *       ephy_file_tmp_dir             (void);
char       *       ephy_file_tmp_filename        (const char  *base,
                                                  const char  *extension);
gboolean           ephy_ensure_dir_exists        (const char  *dir,
                                                  GError **);
GSList     *       ephy_file_find                (const char  *path,
                                                  const char  *fname,
                                                  gint         maxdepth);
gboolean           ephy_file_switch_temp_file    (GFile       *file_dest,
                                                  GFile       *file_temp);
void               ephy_file_delete_on_exit      (GFile       *file);
EphyMimePermission ephy_file_check_mime          (const char  *mime_type);
gboolean           ephy_file_launch_desktop_file (const char  *filename,
                                                  const char  *parameter,
                                                  guint32      user_time,
                                                  GtkWidget   *widget);
gboolean           ephy_file_launch_application  (GAppInfo    *app,
                                                  GList       *files,
                                                  guint32      user_time,
                                                  GtkWidget   *widget);
gboolean           ephy_file_launch_handler      (const char  *mime_type,
                                                  GFile       *file,
                                                  guint32      user_time);
gboolean           ephy_file_browse_to           (GFile       *file,
                                                  guint32      user_time);
gboolean           ephy_file_delete_dir_recursively (GFile *file,
                                                     GError      **error);
gboolean           ephy_file_move_dir_recursively (GFile *source,
						   GFile *destination,
						   GError **error);
void               ephy_file_delete_uri          (const char  *uri);

G_END_DECLS

#endif /* EPHY_FILE_HELPERS_H */
