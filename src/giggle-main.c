/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Imendio AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include "giggle-window.h"
#include "giggle-clone-dialog.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <locale.h>

static gboolean diff_window = FALSE;
static gboolean version = FALSE;
static gchar  *clone_uri = NULL;
static gchar **dirs = NULL;

static GOptionEntry options[] = {
	{ "clone", 'c',
	  0, G_OPTION_ARG_FILENAME, &clone_uri,
	  N_("Clone a repository"),
	  N_("URL") },
	{ "diff", 'd',
	  0, G_OPTION_ARG_NONE, &diff_window,
	  N_("Show the diff window"),
	  NULL },
	{ "version", 'v',
	  0, G_OPTION_ARG_NONE, &version,
	  N_("Show version information and exit"), NULL },
	{ G_OPTION_REMAINING, '\0',
	  0, G_OPTION_ARG_FILENAME_ARRAY, &dirs,
	  NULL, N_("[DIRECTORY]") },
	{ NULL }
};

int
main (int    argc,
      char **argv)
{
	GtkWidget      *window = NULL;
	GError         *error = NULL;
	GOptionContext *context;
	char           *dir, *description;
	int             result = EXIT_SUCCESS;
	
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
	setlocale (LC_ALL, "");

	g_thread_init (NULL);

	gdk_threads_init ();
	gdk_threads_enter ();

	context = g_option_context_new (NULL);

	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));

	description = g_strdup_printf (_("Report errors (in English, with LC_ALL=C) to <%s>."), PACKAGE_BUGREPORT);
	g_option_context_set_summary (context, N_("Giggle is a graphical frontend for the git content tracker."));
	g_option_context_set_description (context, description);
	g_free (description);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		char *basename = g_filename_display_basename (argv[0]);

		g_printerr ("%s: %s\n", basename, error->message);
		g_printerr (_("Try `%s --help' for more information.\n"), basename);

		result = EXIT_FAILURE;
		g_free (basename);

		goto end;
	}
	/* Freing memory */
	g_option_context_free (context);

	gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default (), ICONDIR);

	if (g_file_test ("pixmaps", G_FILE_TEST_IS_DIR))
		gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default (), "pixmaps");

	if (version) {
		g_print ("%s %s, "
			 "Copyright (C) 2007-2008 Imendio AB, "
			 "Copyright (C) 2008 Mathias Hasselmann\n",
			 PACKAGE_NAME, PACKAGE_VERSION);

		goto end;
	}

	gtk_window_set_default_icon_name (PACKAGE);
	g_set_application_name ("Giggle");
	window = giggle_window_new ();

	/* Set dir to:
	    - the first remaining arg, or
	    - the value of GIT_DIR, or
	    - the current working dir */
	if (dirs && *dirs) {
		dir = g_strdup (*dirs);
		g_strfreev (dirs);
	} else {
		dir = g_strdup (g_getenv ("GIT_DIR"));
		if (dir == NULL) {
			dir = g_get_current_dir ();
		}
	}
	g_unsetenv ("GIT_DIR");

	if (clone_uri) {
		GtkWidget *clone_dialog;
		GtkResponseType response;
		gchar *clone_dir;

		clone_dir = g_strdup (dir);
		clone_dialog = giggle_clone_dialog_new (clone_uri, clone_dir);
		g_free (clone_dir);

		response = gtk_dialog_run (GTK_DIALOG (clone_dialog));
		if (response == GTK_RESPONSE_ACCEPT) {
			/* replace dir with the the newly cloned repo's one */
			g_free (dir);
			dir = g_strdup (giggle_clone_dialog_get_directory (GIGGLE_CLONE_DIALOG (clone_dialog)));
		}
		gtk_widget_destroy (clone_dialog);
	}

	if (giggle_git_test_dir (dir)) {
		giggle_window_set_directory (GIGGLE_WINDOW (window), dir);
	} else {
		gtk_widget_show (window);
	}
	g_free (dir);

	/* window will show itself when it reads its initial size configuration */
	if (diff_window)
		giggle_window_show_diff_window (GIGGLE_WINDOW (window));

	gtk_main ();

end:
	if (window)
		gtk_widget_destroy (window);

	gdk_threads_leave ();

	return result;
}
