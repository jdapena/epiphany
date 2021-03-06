SUBDIRS = bookmarks

NULL =

noinst_LTLIBRARIES = libephymain.la

bin_PROGRAMS = epiphany

EXTRA_DIST = \
	epiphany.gresource.xml \
	$(UI_FILES)            \
	$(NULL)

headerdir = $(prefix)/include/epiphany/$(EPIPHANY_MAJOR)/epiphany
header_DATA = \
	ephy-type-builtins.h	\
	$(INST_H_FILES)

NOINST_H_FILES = \
	ephy-action-helper.h			\
	ephy-combined-stop-reload-action.h	\
	ephy-encoding-dialog.h			\
	ephy-encoding-menu.h			\
	ephy-find-toolbar.h			\
	ephy-history-window.h			\
	ephy-home-action.h			\
	ephy-link-action.h			\
	ephy-lockdown.h				\
	ephy-location-controller.h		\
	ephy-navigation-history-action.h	\
	ephy-page-menu-action.h			\
	ephy-password-info.h			\
	ephy-private.h				\
	ephy-toolbar.h				\
	ephy-window-action.h			\
	languages.h				\
	pdm-dialog.h				\
	popup-commands.h			\
	prefs-dialog.h				\
	window-commands.h

INST_H_FILES = \
	ephy-extension.h		\
	ephy-extensions-manager.h	\
	ephy-link.h			\
	ephy-notebook.h			\
	ephy-session.h			\
	ephy-shell.h			\
	ephy-window.h			\
	epiphany.h			\
	$(NULL)

libephymain_la_SOURCES = \
	ephy-action-helper.c			\
	ephy-completion-model.c			\
	ephy-completion-model.h			\
	ephy-combined-stop-reload-action.c	\
	ephy-encoding-dialog.c			\
	ephy-encoding-menu.c			\
	ephy-extension.c			\
	ephy-extensions-manager.c		\
	ephy-find-toolbar.c			\
	ephy-home-action.c			\
	ephy-history-window.c			\
	ephy-link.c				\
	ephy-link-action.c			\
	ephy-location-controller.c		\
	ephy-lockdown.c				\
	ephy-navigation-history-action.c	\
	ephy-notebook.c				\
	ephy-page-menu-action.c			\
	ephy-password-info.c	        	\
	ephy-session.c				\
	ephy-shell.c				\
	ephy-toolbar.c				\
	ephy-window.c				\
	ephy-window-action.c			\
	pdm-dialog.c				\
	popup-commands.c			\
	prefs-dialog.c				\
	window-commands.c			\
	$(INST_H_FILES)				\
	$(NOINST_H_FILES)			\
	$(NULL)

nodist_libephymain_la_SOURCES = \
	$(TYPES_SOURCE)

libephymain_la_CPPFLAGS = \
	-I$(top_builddir)/lib		\
	-I$(top_builddir)/embed		\
	-I$(top_srcdir)/embed 		\
	-I$(top_srcdir)/lib   		\
	-I$(top_srcdir)/lib/egg		\
	-I$(top_srcdir)/lib/history	\
	-I$(top_srcdir)/lib/widgets   	\
	-I$(top_srcdir)/src/bookmarks   \
	-DEXTENSIONS_DIR=\""$(libdir)/epiphany/$(EPIPHANY_API_VERSION)/extensions"\" 	\
	-DLOADER_DIR=\""$(libdir)/epiphany/$(EPIPHANY_MAJOR)/loaders"\"		\
	-DDATADIR=\""$(pkgdatadir)"\" 	\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"	\
	$(AM_CPPFLAGS)

if WITH_WEBKIT2
libephymain_la_CPPFLAGS += -DHAVE_WEBKIT2
endif

libephymain_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS) 	  \
	$(WEBKIT_CFLAGS)	  \
	$(AM_CFLAGS)

UI_FILES = \
	resources/epiphany-bookmark-editor-ui.xml \
	resources/epiphany-history-window-ui.xml  \
	resources/epiphany-ui.xml		  \
	resources/epiphany.ui			  \
	resources/epiphany-application-menu.ui	  \
	resources/prefs-dialog.ui		  \
	resources/epiphany.css                    \
	$(NULL)

epiphany-resources.c: epiphany.gresource.xml $(UI_FILES)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir)/resources --generate-source --c-name epiphany $(srcdir)/epiphany.gresource.xml

epiphany-resources.h: epiphany.gresource.xml $(UI_FILES)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir)/resources --generate-header --c-name epiphany $(srcdir)/epiphany.gresource.xml

EPIPHANY_RESOURCES = \
	epiphany-resources.c \
	epiphany-resources.h \
	$(NULL)

BUILT_SOURCES = \
	$(EPIPHANY_RESOURCES) \
	$(TYPES_SOURCE)       \
	$(NULL)

if ENABLE_SEED
NOINST_H_FILES += \
	ephy-seed-extension.h \
	ephy-seed-loader.h \
	$(NULL)

libephymain_la_SOURCES += \
	ephy-seed-loader.c \
	ephy-seed-extension.c \
	$(NULL)

libephymain_la_CFLAGS += $(SEED_CFLAGS)
endif # ENABLE_SEED

epiphany_SOURCES = ephy-main.c \
	$(EPIPHANY_RESOURCES)  \
	$(NULL)

epiphany_CPPFLAGS = \
	-I$(top_builddir)/lib		\
	-I$(top_srcdir)/embed 		\
	-I$(top_srcdir)/lib   		\
	-I$(top_srcdir)/lib/egg		\
	-I$(top_srcdir)/lib/history	\
	-I$(top_srcdir)/src/bookmarks   \
	-DDATADIR=\""$(datadir)"\" 	\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"	\
	$(INCINTL)			\
	$(AM_CPPFLAGS)

epiphany_CFLAGS = \
	$(DEPENDENCIES_CFLAGS) 	\
	$(AM_CFLAGS)

epiphany_LDFLAGS = -dlopen self

epiphany_LDADD = \
	libephymain.la \
	$(top_builddir)/src/bookmarks/libephybookmarks.la \
	$(top_builddir)/embed/libephyembed.la \
	$(top_builddir)/lib/widgets/libephywidgets.la \
	$(top_builddir)/lib/history/libephyhistory.la \
	$(top_builddir)/lib/libephymisc.la \
	$(top_builddir)/lib/egg/libegg.la \
	$(DEPENDENCIES_LIBS) \
	$(LIBINTL)

if !WITH_WEBKIT2
if HAVE_INTROSPECTION
epiphany_CFLAGS += $(INTROSPECTION_CFLAGS)
epiphany_LDADD += $(INTROSPECTION_LIBS)
endif
endif

if ENABLE_SEED
epiphany_LDADD += $(SEED_LIBS)
endif # ENABLE_SEED

TYPES_SOURCE = \
	ephy-type-builtins.c	\
	ephy-type-builtins.h

stamp_files = \
	stamp-ephy-type-builtins.c	\
	stamp-ephy-type-builtins.h

ephy-type-builtins.c: stamp-ephy-type-builtins.c Makefile
	@true
stamp-ephy-type-builtins.c: Makefile $(INST_H_FILES) $(NOINST_H_FILES)
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include <config.h>\n\n" \
		--fhead "#include \"ephy-type-builtins.h\"\n\n" \
		--fprod "\n/* enumerations from \"@filename@\" */" \
		--fprod "\n#include \"@filename@\"" \
		--vhead "GType\n@enum_name@_get_type (void)\n{\n" \
		--vhead "  static GType type = 0;\n\n" \
		--vhead "  if (G_UNLIKELY (type == 0))\n  {\n" \
		--vhead "    static const G@Type@Value _@enum_name@_values[] = {" \
		--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
		--vtail "      { 0, NULL, NULL }\n    };\n\n" \
		--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n  }\n\n" \
		--vtail "  return type;\n}\n\n" \
		$(filter-out $<,$^) > xgen-$(@F) \
	&& ( cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%) ) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

ephy-type-builtins.h: stamp-ephy-type-builtins.h Makefile
	@true
stamp-ephy-type-builtins.h: Makefile $(INST_H_FILES) $(NOINST_H_FILES)
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#if !defined (__EPHY_EPIPHANY_H_INSIDE__) && !defined (EPIPHANY_COMPILATION)\n" \
		--fhead "#error Only <epiphany/epiphany.h> can be included directly.\n" \
		--fhead "#endif\n\n" \
		--fhead "#ifndef EPHY_TYPE_BUILTINS_H\n" \
		--fhead "#define EPHY_TYPE_BUILTINS_H 1\n\n" \
		--fhead "#include <glib-object.h>\n\n" \
		--fhead "G_BEGIN_DECLS\n\n" \
		--ftail "G_END_DECLS\n\n" \
		--ftail "#endif /* EPHY_TYPE_BUILTINS_H */\n" \
		--fprod "\n/* --- @filename@ --- */" \
		--eprod "#define EPHY_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
		--eprod "GType @enum_name@_get_type (void);\n" \
		$(filter-out $<,$^) > xgen-$(@F) \
	&& ( cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%) ) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

CLEANFILES = $(stamp_files) $(BUILT_SOURCES)
DISTCLEANFILES = $(stamp_files) $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(stamp_files) $(BUILT_SOURCES)

if !WITH_WEBKIT2
if HAVE_INTROSPECTION
EPHY_GIR_H_FILES = \
	$(top_srcdir)/embed/ephy-download.h \
	$(top_srcdir)/embed/ephy-embed.h \
	$(top_srcdir)/embed/ephy-embed-container.h \
	$(top_srcdir)/embed/ephy-embed-event.h \
	$(top_srcdir)/embed/ephy-embed-shell.h \
	$(top_srcdir)/embed/ephy-embed-single.h \
	$(top_srcdir)/embed/ephy-permission-manager.h \
	$(top_srcdir)/embed/ephy-web-view.h \
	$(top_srcdir)/lib/ephy-dialog.h \
	$(top_srcdir)/lib/ephy-node-db.h \
	$(top_srcdir)/lib/ephy-node.h \
	$(top_srcdir)/lib/ephy-state.h \
	$(top_srcdir)/lib/widgets/ephy-download-widget.h \
	$(top_srcdir)/lib/widgets/ephy-search-entry.h \
	$(top_srcdir)/src/bookmarks/ephy-bookmarks.h \
	$(top_srcdir)/src/ephy-extension.h \
	$(top_srcdir)/src/ephy-extensions-manager.h \
	$(top_srcdir)/src/ephy-find-toolbar.h \
	$(top_srcdir)/src/ephy-link-action.h \
	$(top_srcdir)/src/ephy-link.h \
	$(top_srcdir)/src/ephy-location-controller.h \
	$(top_srcdir)/src/ephy-notebook.h \
	$(top_srcdir)/src/ephy-session.h \
	$(top_srcdir)/src/ephy-shell.h \
	$(top_srcdir)/src/ephy-window.h \
	$(NULL)

EPHY_GIR_C_FILES = \
	$(top_srcdir)/embed/ephy-download.c \
	$(top_srcdir)/embed/ephy-embed.c \
	$(top_srcdir)/embed/ephy-embed-container.c \
	$(top_srcdir)/embed/ephy-embed-event.c \
	$(top_srcdir)/embed/ephy-embed-shell.c \
	$(top_srcdir)/embed/ephy-embed-single.c \
	$(top_srcdir)/embed/ephy-permission-manager.c \
	$(top_srcdir)/embed/ephy-web-view.c \
	$(top_srcdir)/lib/ephy-dialog.c \
	$(top_srcdir)/lib/ephy-node-db.c \
	$(top_srcdir)/lib/ephy-node.c \
	$(top_srcdir)/lib/ephy-state.c \
	$(top_srcdir)/lib/widgets/ephy-download-widget.c \
	$(top_srcdir)/lib/widgets/ephy-search-entry.c \
	$(top_srcdir)/src/bookmarks/ephy-bookmarks.c \
	$(top_srcdir)/src/ephy-extension.c \
	$(top_srcdir)/src/ephy-extensions-manager.c \
	$(top_srcdir)/src/ephy-find-toolbar.c \
	$(top_srcdir)/src/ephy-link-action.c \
	$(top_srcdir)/src/ephy-link.c \
	$(top_srcdir)/src/ephy-location-controller.c \
	$(top_srcdir)/src/ephy-notebook.c \
	$(top_srcdir)/src/ephy-session.c \
	$(top_srcdir)/src/ephy-shell.c \
	$(top_srcdir)/src/ephy-window.c \
	$(NULL)

Epiphany-$(EPIPHANY_API_VERSION).gir: $(INTROSPECTION_SCANNER) $(EPHY_GIR_H_FILES) \
                                      $(EPHY_GIR_C_FILES) epiphany
	$(AM_V_GEN) PKG_CONFIG_PATH=$(top_builddir)/data:$$PKG_CONFIG_PATH \
	$(INTROSPECTION_SCANNER) -v --warn-all \
		--identifier-prefix=Ephy \
		--symbol-prefix=ephy \
		--namespace=Epiphany \
		--nsversion=$(EPIPHANY_API_VERSION) \
		--add-include-path=. \
		--add-include-path=$(srcdir) \
		--include=Gtk-3.0 \
		--include=libxml2-2.0 \
		--include=WebKit-3.0 \
		--program=./epiphany \
		--output=$@ \
		--pkg=epiphany-$(EPIPHANY_API_VERSION) \
		-DEPIPHANY_COMPILATION \
		-I$(top_srcdir)/embed \
		-I$(top_srcdir)/lib \
		-I$(top_srcdir)/lib/egg \
		-I$(top_srcdir)/src \
		-I$(top_srcdir)/src/bookmarks \
		$(EPHY_GIR_H_FILES) \
		$(EPHY_GIR_C_FILES)

girdir = $(datadir)/gir-1.0
gir_DATA = Epiphany-$(EPIPHANY_API_VERSION).gir

typelibsdir = $(libdir)/girepository-1.0
typelibs_DATA = Epiphany-$(EPIPHANY_API_VERSION).typelib

Epiphany-$(EPIPHANY_API_VERSION).typelib: Epiphany-$(EPIPHANY_API_VERSION).gir $(INTROSPECTION_COMPILER)
	$(AM_V_GEN) $(INTROSPECTION_COMPILER) Epiphany-$(EPIPHANY_API_VERSION).gir --includedir=$(top_builddir)/src --includedir=$(srcdir) -o Epiphany-$(EPIPHANY_API_VERSION).typelib

CLEANFILES += $(gir_DATA) $(typelibs_DATA)

endif # HAVE_INTROSPECTION
endif # !WITH_WEBKIT2
