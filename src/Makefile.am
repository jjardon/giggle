AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DICONDIR='"$(datadir)/icons"' \
	-DGLADEDIR='"$(pkgdatadir)/glade"' \
	$(GIGGLE_CFLAGS) $(PLATFORM_CFLAGS) \
	$(WARN_CFLAGS)

AM_LDFLAGS = \
	$(PLATFORM_LDFLAGS) \
	-Wl,--export-dynamic -rpath '$(libdir)'

bin_PROGRAMS = giggle

giggle_SOURCES = \
	eggfindbar.c \
	eggfindbar.h \
	giggle-authors-view.c \
	giggle-authors-view.h \
	giggle-avatar-cache.c \
	giggle-avatar-cache.h \
	giggle-avatar-image.c \
	giggle-avatar-image.h \
	giggle-branches-view.c \
	giggle-branches-view.h \
	giggle-clone-dialog.c \
	giggle-clone-dialog.h \
	giggle-description-editor.c \
	giggle-description-editor.h \
	giggle-diff-view.c \
	giggle-diff-view.h \
	giggle-diff-tree-view.c \
	giggle-diff-tree-view.h \
	giggle-diff-window.c \
	giggle-diff-window.h \
	giggle-file-list.c \
	giggle-file-list.h \
	giggle-graph-renderer.c \
	giggle-graph-renderer.h \
	giggle-helpers.c \
	giggle-helpers.h \
	giggle-input-dialog.c \
	giggle-input-dialog.h \
	giggle-label-action.c \
	giggle-label-action.h \
	giggle-remote-editor.c \
	giggle-remote-editor.h \
	giggle-remotes-view.c \
	giggle-remotes-view.h \
	giggle-rev-list-view.c \
	giggle-rev-list-view.h \
	giggle-revision-info.c \
	giggle-revision-info.h \
	giggle-revision-info-action.c \
	giggle-revision-info-action.h \
	giggle-revision-view.c \
	giggle-revision-view.h \
	giggle-short-list.c \
	giggle-short-list.h \
	giggle-spaning-renderer.c \
	giggle-spaning-renderer.h \
	giggle-view-diff.c \
	giggle-view-diff.h \
	giggle-view-file.c \
	giggle-view-file.h \
	giggle-view-history.c \
	giggle-view-history.h \
	giggle-view-summary.c \
	giggle-view-summary.h \
	giggle-window.c \
	giggle-window.h \
	giggle-main.c \
	$(NULL)

giggle_LDADD = \
	../libgiggle-git/libgiggle-git.la

if IGE_PLATFORM_OSX

giggle_SOURCES += \
	ige-mac-menu.c \
	ige-mac-menu.h \
	$(NULL)

endif

#test_graph_renderer_SOURCES = \
#	giggle-revision.c giggle-revision.h \
#	giggle-graph-renderer.c	giggle-graph-renderer.h \
#	test-graph-renderer.c


