#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <libintl.h>
#include <locale.h>
#include <langinfo.h>
#include <glib/gi18n-lib.h>
#include <libgnome-control-center/cc-panel.h>

#include "ephy-embed.h"
#include "ephy-embed-prefs.h"
#include "ephy-embed-shell.h"
#include "ephy-file-helpers.h"
#include "ephy-shell.h"
#include "ephy-web-view.h"

#define EPHY_TYPE_CC_APPLICATIONS_PANEL ephy_cc_applications_panel_get_type()

typedef struct _EphyCcApplicationsPanel EphyCcApplicationsPanel;
typedef struct _EphyCcApplicationsPanelClass EphyCcApplicationsPanelClass;

struct _EphyCcApplicationsPanel
{
  CcPanel parent;
};

struct _EphyCcApplicationsPanelClass
{
  CcPanelClass parent_class;
};

G_DEFINE_DYNAMIC_TYPE (EphyCcApplicationsPanel, ephy_cc_applications_panel, CC_TYPE_PANEL)

static void
ephy_cc_applications_panel_init (EphyCcApplicationsPanel *self)
{
  GtkWidget *embed;
  EphyWebView *web_view;

  embed = g_object_new (EPHY_TYPE_EMBED, NULL);
  web_view = ephy_embed_get_web_view (EPHY_EMBED (embed));
  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), "about:blank");
  g_object_set (G_OBJECT (web_view),
		"popups-allowed", FALSE,
		NULL);
  ephy_web_view_load_url (web_view, "ephy-about:applications");

  gtk_widget_show (embed);
  gtk_container_add (GTK_CONTAINER (self), embed);
}

static void
ephy_cc_applications_panel_dispose (GObject * object)
{
}

static void
ephy_cc_applications_panel_class_finalize (EphyCcApplicationsPanelClass *klass)
{
}

static void
ephy_cc_applications_panel_class_init (EphyCcApplicationsPanelClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = ephy_cc_applications_panel_dispose;
}


void
g_io_module_load (GIOModule *module)
{
  EphyShellStartupContext *ctx;
  GError *error = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  ephy_embed_prefs_init ();

  ephy_file_helpers_init (NULL,
			  FALSE,
			  FALSE,
			  &error);
  g_assert_no_error (error);

  _ephy_shell_create_instance (FALSE);

  ctx = ephy_shell_startup_context_new (0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        0);
  ephy_shell_set_startup_context (ephy_shell, ctx);
  ephy_cc_applications_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  EPHY_TYPE_CC_APPLICATIONS_PANEL,
                                  "epiphany-applications", 0);
}

void
g_io_module_unload (GIOModule *module)
{
}
