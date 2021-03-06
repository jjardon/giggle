libgiggle_gitdir = $(pkgincludedir)/libgiggle-git

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DGLADEDIR='"$(pkgdatadir)/glade"' \
	$(GIGGLE_CFLAGS) $(PLATFORM_CFLAGS) \
	$(WARN_CFLAGS)

lib_LTLIBRARIES = libgiggle-git.la

libgiggle_git_h_files =  \
        giggle-git.h \
	giggle-git-add.h \
	giggle-git-add-ref.h \
	giggle-git-authors.h \
	giggle-git-blame.h \
	giggle-git-cat-file.h \
	giggle-git-clone.h \
	giggle-git-commit.h \
	giggle-git-config.h \
	giggle-git-config-read.h \
	giggle-git-config-write.h \
	giggle-git-delete-ref.h \
	giggle-git-diff-tree.h \
	giggle-git-diff.h \
	giggle-git-ignore.h \
	giggle-git-list-files.h \
	giggle-git-list-tree.h \
	giggle-git-log.h \
	giggle-git-refs.h \
	giggle-git-remote-list.h \
	giggle-git-revisions.h \
	$(NULL)

libgiggle_git_la_SOURCES = \
	$(libgiggle_git_h_files) \
	$(BUILT_SOURCES) \
	giggle-git-add-ref.c \
	giggle-git-add.c \
	giggle-git-authors.c \
	giggle-git-blame.c \
	giggle-git-cat-file.c \
	giggle-git-clone.c \
	giggle-git-commit.c \
	giggle-git-config.c \
	giggle-git-config-read.c \
	giggle-git-config-write.c \
	giggle-git-delete-ref.c \
	giggle-git-diff-tree.c \
	giggle-git-diff.c \
	giggle-git-ignore.c \
	giggle-git-list-files.c \
	giggle-git-list-tree.c \
	giggle-git-log.c \
	giggle-git-refs.c \
	giggle-git-remote-list.c \
	giggle-git-revisions.c \
        giggle-git.c \
	$(NULL)
	 
libgiggle_git_HEADERS = \
	$(libgiggle_git_h_files) \
	giggle-git-enums.h \
	$(NULL)

libgiggle_git_la_LIBADD = \
	../libgiggle/libgiggle.la \
	$(GIGGLE_LIBS)

libgiggle_git_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-info $(LIBGIGGLE_GIT_ABI) \
	-Wl,--as-needed -Wl,--no-undefined \
	$(NULL)

giggle-git-enums.h: giggle-git-enums.h.in $(libgiggle_git_h_files)
	$(AM_V_at)( cd $(srcdir) && $(GLIB_MKENUMS) --template $^ ) > xgen-gtbh
	$(AM_V_at)( cmp -s xgen-gtbh $@ || (echo "  ENUM  $@..."; cp xgen-gtbh $@ ))
	$(AM_V_at)( rm -f xgen-gtbh )

giggle-git-enums.c: giggle-git-enums.c.in $(libgiggle_git_h_files)
	$(AM_V_at)( cd $(srcdir) && $(GLIB_MKENUMS) --template $^ ) > xgen-gtbc
	$(AM_V_at)( cmp -s xgen-gtbc $@ || (echo "  ENUM  $@..."; cp xgen-gtbc $@ ))
	$(AM_V_at)( rm -f xgen-gtbc )

BUILT_SOURCES = \
	giggle-git-enums.h \
	giggle-git-enums.c

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = \
	giggle-git-enums.c.in \
	giggle-git-enums.h.in
