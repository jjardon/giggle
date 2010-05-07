gladedir = $(datadir)/giggle/glade
dist_glade_DATA = \
	main-window.ui

desktop_in_data=giggle.desktop.in
desktop_DATA=$(desktop_in_data:.desktop.in=.desktop)
desktopdir=$(datadir)/applications
@INTLTOOL_DESKTOP_RULE@

schemas_in_data=giggle.schemas.in
schemas_DATA=$(schemas_in_data:.schemas.in=.schemas)
schemasdir=$(GCONF_SCHEMA_FILE_DIR)
@INTLTOOL_SCHEMAS_RULE@

# don't do this if we are building in eg. rpm
if GCONF_SCHEMAS_INSTALL
install-data-local:
	@if test -z "$(DESTDIR)"; then \
		GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) \
		$(GCONFTOOL) --makefile-install-rule \
		$(builddir)/$(schemas_DATA); \
	fi

uninstall-local:
	@if test -z "$(DESTDIR)"; then \
		GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) \
		$(GCONFTOOL) --makefile-uninstall-rule \
		$(builddir)/$(schemas_DATA); \
	fi
endif

CLEANFILES=$(desktop_DATA) $(schemas_DATA)

EXTRA_DIST=giggle.desktop.in giggle.schemas.in
