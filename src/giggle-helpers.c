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

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <glib/gi18n.h>

#include "giggle-helpers.h"
#include "libgiggle-git/giggle-git.h"

static void
remote_editor_tree_selection_get_branches (GtkTreeModel *model,
					   GtkTreePath  *path,
					   GtkTreeIter  *iter,
					   GList       **branches)
{
	*branches = g_list_prepend (*branches, gtk_tree_row_reference_new (model, path));
}

static void
remote_editor_remove_branch (GtkTreeRowReference *ref)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;

	model = gtk_tree_row_reference_get_model (ref);
	gtk_tree_model_get_iter (model, &iter,
				 gtk_tree_row_reference_get_path (ref));
	gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

	gtk_tree_row_reference_free (ref);
}

/* returns TRUE if the key press was delete and at least one row has been
 * deleted */
gboolean
giggle_list_view_delete_selection (GtkWidget   *treeview,
				   GdkEventKey *event)
{
	if (event->keyval == GDK_Delete) {
		GtkTreeSelection* sel;
		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

		if (gtk_tree_selection_count_selected_rows (sel) > 0) {
			GList* branches = NULL;
			gtk_tree_selection_selected_foreach (sel,
							     (GtkTreeSelectionForeachFunc)
								remote_editor_tree_selection_get_branches,
							     &branches);
			g_list_foreach (branches, (GFunc)remote_editor_remove_branch, NULL);
			g_list_free (branches);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
tree_model_find_string (GtkTreeModel *model,
			GtkTreeIter  *iter,
			GtkTreeIter  *parent,
			int           column,
			const char   *pattern)
{
	GtkTreeIter  child;
	char        *text;

	if (gtk_tree_model_iter_children (model, iter, parent)) {
		do {
			gtk_tree_model_get (model, iter, column, &text, -1);

			if (!g_strcmp0 (text, pattern)) {
				g_free (text);
				return TRUE;
			}

			g_free (text);

			if (tree_model_find_string (model, &child, iter, column, pattern)) {
				*iter = child;
				return TRUE;
			}
		} while (gtk_tree_model_iter_next (model, iter));
	}

	return FALSE;
}

gboolean
giggle_tree_view_select_row_by_string (GtkWidget  *treeview,
				       int         column,
				       const char *pattern)
{
	GtkTreeSelection   *selection;
	GtkTreeModel	   *model;
	GtkTreeIter	    iter;
	GtkTreePath        *path;
	char               *text;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, column, &text, -1);
	} else {
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
		text = NULL;
	}

	if (!g_strcmp0 (text, pattern)) {
		g_free (text);
		return TRUE;
	}
	g_free (text);
	text = NULL;

	if (tree_model_find_string (model, &iter, NULL, column, pattern)) {
		path = gtk_tree_model_get_path (model, &iter);
		gtk_tree_view_expand_to_path (GTK_TREE_VIEW (treeview), path);
	 	gtk_tree_path_free (path);

		gtk_tree_selection_select_iter (selection, &iter);

		return TRUE;
	}

	return FALSE;
}

GtkActionGroup *
giggle_ui_manager_get_action_group (GtkUIManager *manager,
				    const char   *group_name)
{
	GList *groups;

	groups = gtk_ui_manager_get_action_groups (manager);

	while (groups) {
		if (!g_strcmp0 (group_name,
				gtk_action_group_get_name (groups->data)))
			return groups->data;

		groups = groups->next;
	}

	return NULL;
}

void
giggle_error_dialog (GtkWindow *window,
                     GError    *error)
{
	GtkWidget *widget;

	widget = gtk_message_dialog_new (window,
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_ERROR,
	                                 GTK_BUTTONS_CLOSE,
	                                 "%s", _("An error ocurred:"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (widget),
	                                          "%s", error->message);

	gtk_dialog_run (GTK_DIALOG (widget));
	gtk_widget_destroy (widget);
}

void
giggle_open_file (GtkWidget  *widget,
                  const char *directory,
                  const char *filename)
{
	GError *error = NULL;
	char   *path, *uri;

	g_return_if_fail (NULL != filename);

	if (!directory)
		directory = giggle_git_get_directory (giggle_git_get ());

	g_return_if_fail (NULL != directory);

	path = g_build_filename (directory, filename, NULL);
	uri = g_filename_to_uri (path, NULL, &error);

	gtk_show_uri (gtk_widget_get_screen (widget),
	              uri,  gtk_get_current_event_time (),
	              &error);
	if (error != NULL) {
		giggle_error_dialog (GTK_WINDOW (widget), error);
		g_clear_error (&error);
	}

	g_free (path);
	g_free (uri);
}

static gboolean
delete_directory_recursive (GFile *dir, GError **error)
{
	gchar *uri;
	GFileEnumerator *file_enum;
	GFileInfo *info;
	gboolean _failure = FALSE;

	if (error != NULL)
		*error = NULL;

	file_enum = g_file_enumerate_children (dir,
					       G_FILE_ATTRIBUTE_STANDARD_NAME ","
					       G_FILE_ATTRIBUTE_STANDARD_TYPE,
					       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);

	uri = g_file_get_uri (dir);
	while (! _failure &&
               (info = g_file_enumerator_next_file (file_enum, NULL, error))) {
		gchar *child_uri;
		GFile *child;

		child_uri = g_build_filename (uri, g_file_info_get_name (info), NULL);
		child = g_file_new_for_uri (child_uri);
		g_free (child_uri);

		switch (g_file_info_get_file_type (info)) {
		case G_FILE_TYPE_DIRECTORY:
			if (! delete_directory_recursive (child, error))
				_failure = TRUE;
			break;
		default:
			if (! g_file_delete (child, NULL, error))
				_failure = TRUE;
			break;
		}

		g_object_unref (child);
		g_object_unref (info);
	}
	if (file_enum)
		g_object_unref (file_enum);
	g_free (uri);

	if (! _failure && ! g_file_delete (dir, NULL, error))
		_failure = TRUE;

	return !_failure;
}

void
giggle_remove_directory_recursive (const gchar *path)
{
	GFile *dir;
	GError *error = NULL;

	dir = g_file_new_for_path (path);
	delete_directory_recursive (dir, &error);
	if (error) {
		g_warning ("Cannot delete %s: %s", path, error->message);
		g_error_free (error);
	}
	g_object_unref (dir);
}
