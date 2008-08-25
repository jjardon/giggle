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

#include <config.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include "libgiggle/giggle-git.h"
#include "libgiggle/giggle-git-revisions.h"
#include "libgiggle/giggle-git-refs.h"
#include "libgiggle/giggle-git-diff.h"
#include "giggle-view-history.h"
#include "giggle-file-list.h"
#include "giggle-rev-list-view.h"
#include "giggle-revision-view.h"
#include "giggle-diff-view.h"
#include "giggle-diff-tree-view.h"
#include "libgiggle/giggle-searchable.h"
#include "libgiggle/giggle-history.h"

typedef struct GiggleViewHistoryPriv GiggleViewHistoryPriv;

struct GiggleViewHistoryPriv {
	GtkWidget          *main_vpaned;
	GtkWidget          *revision_notebook;
	GtkRadioToolButton *revision_tabs;

	GtkWidget          *revision_list;
	GtkWidget          *revision_view;
	GtkWidget          *diff_view;
	GtkWidget          *diff_tree_view;
	GtkWidget          *diff_view_hpaned;
	GtkWidget          *diff_view_sw;

	GiggleGit          *git;
	GiggleJob          *job;
	GiggleJob          *diff_current_job;

	GList              *history; /* reversed list of history elems */
	GList              *current_history_elem;

	guint               selection_changed_idle;

	guint               compact_mode : 1;
};

enum {
	GIGGLE_VIEW_HISTORY_PAGE_CHANGES,
	GIGGLE_VIEW_HISTORY_PAGE_DETAILS
};

static void     view_history_finalize              (GObject *object);

static void     giggle_view_history_searchable_init             (GiggleSearchableIface *iface);
static void     giggle_view_history_history_init                (GiggleHistoryIface    *iface);

static void     view_history_revision_list_selection_changed_cb (GiggleRevListView *list,
								 GiggleRevision     *revision1,
								 GiggleRevision     *revision2,
								 GiggleViewHistory  *view);
static gboolean view_history_revision_list_key_press_cb         (GiggleRevListView *list,
								 GdkEventKey        *event,
								 GiggleViewHistory  *view);

static gboolean view_history_search                             (GiggleSearchable      *searchable,
								 const gchar           *search_term,
								 GiggleSearchDirection  direction,
								 gboolean               full_search);
static void     view_history_cancel_search                      (GiggleSearchable      *searchable);

static void     view_history_update_revisions                   (GiggleViewHistory  *view);

static void     view_history_path_selected                      (GiggleDiffTreeView *diff_tree_view,
								 const gchar        *path,
								 GiggleViewHistory  *view);

static void     view_history_go_back                            (GiggleHistory      *history);
static gboolean view_history_can_go_back                        (GiggleHistory      *history);
static void     view_history_go_forward                         (GiggleHistory      *history);
static gboolean view_history_can_go_forward                     (GiggleHistory      *history);


G_DEFINE_TYPE_WITH_CODE (GiggleViewHistory, giggle_view_history, GIGGLE_TYPE_VIEW,
			 G_IMPLEMENT_INTERFACE (GIGGLE_TYPE_SEARCHABLE,
						giggle_view_history_searchable_init)
			 G_IMPLEMENT_INTERFACE (GIGGLE_TYPE_HISTORY,
						giggle_view_history_history_init))

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GIGGLE_TYPE_VIEW_HISTORY, GiggleViewHistoryPriv))

enum {
	REVISION_COL_OBJECT,
	REVISION_NUM_COLS
};


static void
giggle_view_history_class_init (GiggleViewHistoryClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = view_history_finalize;

	g_type_class_add_private (object_class, sizeof (GiggleViewHistoryPriv));
}

static void
giggle_view_history_searchable_init (GiggleSearchableIface *iface)
{
	iface->search = view_history_search;
	iface->cancel = view_history_cancel_search;
}

static void
giggle_view_history_history_init (GiggleHistoryIface *iface)
{
	iface->go_back = view_history_go_back;
	iface->can_go_back = view_history_can_go_back;
	iface->go_forward = view_history_go_forward;
	iface->can_go_forward = view_history_can_go_forward;
}

static void
view_history_git_changed (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (view);

	/* clear history */
	g_list_foreach (priv->history, (GFunc) g_free, NULL);
	g_list_free (priv->history);
	priv->history = NULL;
	priv->current_history_elem = NULL;

	view_history_update_revisions (view);
}

static void
view_history_set_busy (GtkWidget *widget,
		       gboolean   busy)
{
	if (!GTK_WIDGET_REALIZED (widget)) {
		return;
	}

	if (busy) {
		GdkCursor *cursor;

		cursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (widget->window, cursor);
		gdk_cursor_unref (cursor);
	} else {
		gdk_window_set_cursor (widget->window, NULL);
	}
}

static void
view_history_git_dir_notify (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;
	GtkTextBuffer         *buffer;

	priv = GET_PRIV (view);

	view_history_update_revisions (view);

	/* empty views */
	giggle_diff_tree_view_set_revisions (GIGGLE_DIFF_TREE_VIEW (priv->diff_tree_view), NULL, NULL);
	giggle_revision_view_set_revision (GIGGLE_REVISION_VIEW (priv->revision_view), NULL);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->diff_view));
	gtk_text_buffer_set_text (buffer, "", -1);
}

static void
view_history_setup_revision_list (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;
	GtkWidget             *scrolled_window;

	priv = GET_PRIV (view);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);

	priv->revision_list = giggle_rev_list_view_new ();
	g_signal_connect (priv->revision_list, "selection-changed",
			  G_CALLBACK (view_history_revision_list_selection_changed_cb), view);
	g_signal_connect (priv->revision_list, "key-press-event",
			  G_CALLBACK (view_history_revision_list_key_press_cb), view);

	gtk_container_add (GTK_CONTAINER (scrolled_window), priv->revision_list);
	gtk_widget_show_all (scrolled_window);

	gtk_paned_pack1 (GTK_PANED (priv->main_vpaned), scrolled_window, TRUE, FALSE);
}

static void
revision_tabs_toggled_cb (GtkRadioToolButton *button,
			  GiggleViewHistory  *view)
{
	GiggleViewHistoryPriv *priv;
	int                    current_page;
	GSList		      *l;
	

	priv = GET_PRIV (view);

	l = gtk_radio_tool_button_get_group (button);

	for (current_page = 0; l; ++current_page, l = l->next) {
		if (gtk_toggle_button_get_active (l->data))
			break;
	}

	current_page = gtk_notebook_get_n_pages (GTK_NOTEBOOK (priv->revision_notebook)) - current_page - 1;
	gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->revision_notebook), current_page);
}

static void
revision_notebook_switch_page_cb (GtkNotebook       *notebook,
				  GtkNotebookPage   *page,
				  int                num_page,
				  GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;
	GSList		      *l;

	priv = GET_PRIV (view);

	num_page = gtk_notebook_get_n_pages (GTK_NOTEBOOK (priv->revision_notebook)) - num_page - 1;
	l = gtk_radio_tool_button_get_group (priv->revision_tabs);
	l = g_slist_nth (l, num_page);

	if (l)
		gtk_toggle_button_set_active (l->data, TRUE);
}

static GtkWidget *
view_history_create_toolbar (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;
	GtkWidget             *toolbar;
	GtkWidget             *label;
	GtkToolItem           *item;

	priv = GET_PRIV (view);

	toolbar = gtk_toolbar_new ();

	item = gtk_tool_button_new_from_stock (GTK_STOCK_GO_UP);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

	item = gtk_tool_button_new_from_stock (GTK_STOCK_GO_DOWN);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);

	item = gtk_tool_item_new ();
	label = gtk_label_new (_("Change <b>1/12</b>"));
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
	gtk_container_add (GTK_CONTAINER (item), label);

	item = gtk_separator_tool_item_new  ();
	gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (item), FALSE);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
	gtk_tool_item_set_expand (item, TRUE);

	item = gtk_radio_tool_button_new_from_stock (NULL, GTK_STOCK_COPY);
	gtk_tool_button_set_label (GTK_TOOL_BUTTON (item), _("_Changes"));
	gtk_tool_button_set_use_underline (GTK_TOOL_BUTTON (item), TRUE);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
	gtk_tool_item_set_is_important (item, TRUE);

	priv->revision_tabs = GTK_RADIO_TOOL_BUTTON (item);
	item = gtk_radio_tool_button_new_with_stock_from_widget (priv->revision_tabs,
								 GTK_STOCK_DIALOG_INFO);
	gtk_tool_button_set_label (GTK_TOOL_BUTTON (item), _("_Details"));
	gtk_tool_button_set_use_underline (GTK_TOOL_BUTTON (item), TRUE);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
	gtk_tool_item_set_is_important (item, TRUE);

	g_signal_connect (priv->revision_tabs, "toggled",
			  G_CALLBACK (revision_tabs_toggled_cb), view);

	return toolbar;
}

static void
view_history_setup_revision_pane (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;
	GtkWidget             *vbox;
	GtkWidget             *scrolled_window;

	priv = GET_PRIV (view);

	gtk_rc_parse_string ("style \"view-history-toolbar-style\""
			     "{"
			     "  GtkToolbar::shadow-type = none"
			     "}"
			     "widget_class \"*.<GiggleViewHistory>.*.GtkToolbar\" "
			     "style \"view-history-toolbar-style\"");

	priv->revision_notebook = gtk_notebook_new ();
	gtk_container_set_border_width (GTK_CONTAINER (priv->revision_notebook), 6);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (priv->revision_notebook), FALSE);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (priv->revision_notebook), FALSE);

	g_signal_connect (priv->revision_notebook, "switch-page",
			  G_CALLBACK (revision_notebook_switch_page_cb), view);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), view_history_create_toolbar (view), FALSE, TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), priv->revision_notebook);
	gtk_paned_pack2 (GTK_PANED (priv->main_vpaned), vbox, FALSE, FALSE);
	gtk_widget_show_all (vbox);

	/* diff file view */
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);

	/* FIXME: fixed sizes suck */
	gtk_widget_set_size_request (scrolled_window, 200, -1);

	priv->diff_tree_view = giggle_diff_tree_view_new ();
	gtk_container_add (GTK_CONTAINER (scrolled_window), priv->diff_tree_view);

	g_signal_connect (priv->diff_tree_view, "path-selected",
			  G_CALLBACK (view_history_path_selected), view);

	/* diff view */
	priv->diff_view_sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->diff_view_sw),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (priv->diff_view_sw), GTK_SHADOW_IN);

	priv->diff_view = giggle_diff_view_new ();
	gtk_container_add (GTK_CONTAINER (priv->diff_view_sw), priv->diff_view);

	/* diff paned */
	priv->diff_view_hpaned = gtk_hpaned_new ();
	gtk_paned_pack1 (GTK_PANED (priv->diff_view_hpaned), scrolled_window, FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (priv->diff_view_hpaned), priv->diff_view_sw, TRUE, FALSE);
	gtk_notebook_append_page (GTK_NOTEBOOK (priv->revision_notebook), priv->diff_view_hpaned, NULL);
	gtk_widget_show_all (priv->diff_view_hpaned);

	/* revision view */
	priv->revision_view = giggle_revision_view_new ();
	gtk_notebook_append_page (GTK_NOTEBOOK (priv->revision_notebook), priv->revision_view, NULL);
	gtk_widget_show_all (priv->revision_view);
}

static void
giggle_view_history_init (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (view);

	/* git interaction */
	priv->git = giggle_git_get ();

	g_signal_connect_swapped (priv->git, "notify::git-dir",
				  G_CALLBACK (view_history_git_dir_notify), view);
	g_signal_connect_swapped (priv->git, "changed",
				  G_CALLBACK (view_history_git_changed), view);

	gtk_widget_push_composite_child ();

	/* widgets */
	priv->compact_mode = FALSE;
	priv->main_vpaned = gtk_vpaned_new ();
	gtk_widget_show (priv->main_vpaned);
	gtk_container_add (GTK_CONTAINER (view), priv->main_vpaned);

	view_history_setup_revision_list (view);
	view_history_setup_revision_pane (view);

	gtk_widget_pop_composite_child ();

}

static void
view_history_finalize (GObject *object)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (object);

	if (priv->job) {
		giggle_git_cancel_job (priv->git, priv->job);
		g_object_unref (priv->job);
		priv->job = NULL;
	}

	if (priv->diff_current_job) {
		giggle_git_cancel_job (priv->git, priv->diff_current_job);
		g_object_unref (priv->diff_current_job);
		priv->diff_current_job = NULL;
	}

	g_list_foreach (priv->history, (GFunc) g_free, NULL);
	g_list_free (priv->history);

	g_object_unref (priv->git);

	G_OBJECT_CLASS (giggle_view_history_parent_class)->finalize (object);
}

typedef struct {
	GiggleViewHistory *view;
	GiggleRevision    *revision1;
	GiggleRevision    *revision2;
} ViewHistorySelectionIdleData;

static gboolean
view_history_selection_changed_idle (ViewHistorySelectionIdleData *data)
{
	GiggleViewHistoryPriv *priv;
	GList                 *files;

	GDK_THREADS_ENTER ();

	priv = GET_PRIV (data->view);
	files = NULL;

	if (priv->current_history_elem) {
		files = g_list_prepend (NULL, g_strdup ((gchar *) priv->current_history_elem->data));
	}

	giggle_diff_view_set_revisions (GIGGLE_DIFF_VIEW (priv->diff_view),
					data->revision1, data->revision2, files);

	giggle_diff_tree_view_set_revisions (GIGGLE_DIFF_TREE_VIEW (priv->diff_tree_view),
					     data->revision1, data->revision2);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
view_history_revision_list_selection_changed_cb (GiggleRevListView *list,
						 GiggleRevision     *revision1,
						 GiggleRevision     *revision2,
						 GiggleViewHistory  *view)
{
	GiggleViewHistoryPriv        *priv;
	ViewHistorySelectionIdleData *data;

	priv = GET_PRIV (view);

	giggle_revision_view_set_revision (
		GIGGLE_REVISION_VIEW (priv->revision_view), revision1);

	if (priv->selection_changed_idle) {
		g_source_remove (priv->selection_changed_idle);
	}

	data = g_new0 (ViewHistorySelectionIdleData, 1);
	data->view = view;
	data->revision1 = revision1;
	data->revision2 = revision2;

	priv->selection_changed_idle =
		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				 (GSourceFunc) view_history_selection_changed_idle,
				 data, g_free);
}

static gboolean
view_history_revision_list_key_press_cb (GiggleRevListView *list,
					 GdkEventKey        *event,
					 GiggleViewHistory  *view)
{
	GiggleViewHistoryPriv *priv;
	GtkAdjustment         *adj;
	gdouble                value;

	priv = GET_PRIV (view);

	if (event->keyval == GDK_space ||
	    event->keyval == GDK_BackSpace) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->revision_notebook),
					       GIGGLE_VIEW_HISTORY_PAGE_CHANGES);

		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->diff_view_sw));

		value = (event->keyval == GDK_space) ?
			adj->value + (adj->page_size * 0.8) :
			adj->value - (adj->page_size * 0.8);

		value = CLAMP (value, adj->lower, adj->upper - adj->page_size);

		g_object_set (adj, "value", value, NULL);

		return TRUE;
	}

	return FALSE;
}

static gboolean
view_history_search (GiggleSearchable      *searchable,
		     const gchar           *search_term,
		     GiggleSearchDirection  direction,
		     gboolean               full_search)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (searchable);

	if (!giggle_searchable_search (GIGGLE_SEARCHABLE (priv->revision_list),
				       search_term, direction, full_search)) {
		return FALSE;
	}

	if (giggle_searchable_search (GIGGLE_SEARCHABLE (priv->revision_view),
				      search_term, direction, full_search)) {
		/* search term is contained in the
		 * revision description, expand it
		 */
		gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->revision_notebook),
					       GIGGLE_VIEW_HISTORY_PAGE_DETAILS);
	} else if (giggle_searchable_search (GIGGLE_SEARCHABLE (priv->diff_view),
					     search_term, direction, full_search)) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->revision_notebook),
					       GIGGLE_VIEW_HISTORY_PAGE_CHANGES);
	}

	return TRUE;
}

static void
view_history_cancel_search (GiggleSearchable *searchable)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (searchable);

	giggle_searchable_cancel (GIGGLE_SEARCHABLE (priv->revision_list));
}

typedef void (AddRefFunc) (GiggleRevision*, GiggleRef*);

static gboolean
view_history_add_refs (GiggleRevision *revision,
		       GList          *list,
		       AddRefFunc      func)
{
	GiggleRef   *ref;
	const gchar *sha1, *sha2;
	gboolean     updated = FALSE;

	sha1 = giggle_revision_get_sha (revision);

	while (list) {
		ref = GIGGLE_REF (list->data);
		sha2 = giggle_ref_get_sha (ref);

		if (strcmp (sha1, sha2) == 0) {
			updated = TRUE;
			(* func) (revision, ref);
		}

		list = list->next;
	}

	return updated;
}

static void
view_history_get_branches_cb (GiggleGit    *git,
			      GiggleJob    *job,
			      GError       *error,
			      gpointer      user_data)
{
	GiggleViewHistory     *view;
	GiggleViewHistoryPriv *priv;
	GiggleRevision        *revision;
	GtkTreeModel          *model;
	GtkTreePath           *path;
	GtkTreeIter            iter;
	gboolean               valid;
	GList                 *branches, *tags, *remotes;
	gboolean               changed;

	view = GIGGLE_VIEW_HISTORY (user_data);
	priv = GET_PRIV (view);

	if (error) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view))),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("An error ocurred when getting the revisions list:\n%s"),
						 error->message);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	} else {
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->revision_list));
		valid = gtk_tree_model_get_iter_first (model, &iter);
		branches = giggle_git_refs_get_branches (GIGGLE_GIT_REFS (job));
		tags = giggle_git_refs_get_tags (GIGGLE_GIT_REFS (job));
		remotes = giggle_git_refs_get_remotes (GIGGLE_GIT_REFS (job));

		while (valid) {
			gtk_tree_model_get (model, &iter,
					    REVISION_COL_OBJECT, &revision,
					    -1);

			if (revision) {
				changed = view_history_add_refs (revision, branches, giggle_revision_add_branch_head);
				changed |= view_history_add_refs (revision, tags, giggle_revision_add_tag);
				changed |= view_history_add_refs (revision, remotes, giggle_revision_add_remote);

				if (changed) {
					path = gtk_tree_model_get_path (model, &iter);
					gtk_tree_model_row_changed (model, path, &iter);
					gtk_tree_path_free (path);
				}

				g_object_unref (revision);
			}

			valid = gtk_tree_model_iter_next (model, &iter);
		}
	}

	g_object_unref (priv->job);
	priv->job = NULL;
}

static void
view_history_diff_current_cb (GiggleGit *git,
			      GiggleJob *job,
			      GError    *error,
			      gpointer   user_data)
{
	GiggleViewHistoryPriv *priv;
	GtkTreeModel          *model;
	GtkTreePath           *path;
	GtkTreeIter            iter;
	const gchar           *text;

	priv = GET_PRIV (user_data);

	/* FIXME: error report missing */
	if (error) {
		return;
	}

	text = giggle_git_diff_get_result (GIGGLE_GIT_DIFF (job));

	if (text && *text) {
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->revision_list));
		gtk_list_store_insert (GTK_LIST_STORE (model), &iter, 0);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    REVISION_COL_OBJECT, NULL,
				    -1);

		path = gtk_tree_model_get_path (model, &iter);
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (priv->revision_list),
					      path, NULL, FALSE, 0.0, 0.0);
	}
}

static void
view_history_get_revisions_cb (GiggleGit    *git,
			       GiggleJob    *job,
			       GError       *error,
			       gpointer      user_data)
{
	GiggleViewHistory     *view;
	GiggleViewHistoryPriv *priv;
	GtkListStore          *store;
	GtkTreeIter            iter;
	GList                 *revisions;

	view = GIGGLE_VIEW_HISTORY (user_data);
	priv = GET_PRIV (view);

	if (error) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view))),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("An error ocurred when getting the revisions list:\n%s"),
						 error->message);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		g_object_unref (priv->job);
		priv->job = NULL;
	} else {
		store = gtk_list_store_new (REVISION_NUM_COLS, GIGGLE_TYPE_REVISION);
		revisions = giggle_git_revisions_get_revisions (GIGGLE_GIT_REVISIONS (job));

		while (revisions) {
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					    REVISION_COL_OBJECT, revisions->data,
					    -1);
			revisions = revisions->next;
		}

		view_history_set_busy (GTK_WIDGET (priv->revision_list), FALSE);
		giggle_rev_list_view_set_model (GIGGLE_REV_LIST_VIEW (priv->revision_list),
						GTK_TREE_MODEL (store));
		g_object_unref (store);
		g_object_unref (priv->job);
		priv->job = NULL;

		/* now get the list of branches */
		priv->job = giggle_git_refs_new ();

		giggle_git_run_job (priv->git,
				    priv->job,
				    view_history_get_branches_cb,
				    view);

		/* and current diff row */
		if (priv->diff_current_job) {
			giggle_git_cancel_job (priv->git, priv->diff_current_job);
			g_object_unref (priv->diff_current_job);
			priv->diff_current_job = NULL;
		}

		priv->diff_current_job = giggle_git_diff_new ();

		giggle_git_run_job (priv->git,
				    priv->diff_current_job,
				    view_history_diff_current_cb,
				    view);

		giggle_history_changed (GIGGLE_HISTORY (view));
	}
}

static void
view_history_update_revisions (GiggleViewHistory  *view)
{
	GiggleViewHistoryPriv *priv;
	GList                 *files;

	priv = GET_PRIV (view);

	view_history_set_busy (GTK_WIDGET (priv->revision_list), TRUE);
	giggle_rev_list_view_set_model (GIGGLE_REV_LIST_VIEW (priv->revision_list), NULL);

	/* get revision list */
	if (priv->job) {
		giggle_git_cancel_job (priv->git, priv->job);
		g_object_unref (priv->job);
		priv->job = NULL;
	}

	if (priv->current_history_elem &&
	    priv->current_history_elem->data) {
		/* we just want one file */
		files = g_list_prepend (NULL, g_strdup ((gchar *) priv->current_history_elem->data));
		priv->job = giggle_git_revisions_new_for_files (files);
	} else {
		priv->job = giggle_git_revisions_new ();
	}

	giggle_git_run_job (priv->git,
			    priv->job,
			    view_history_get_revisions_cb,
			    view);
}

static void
view_history_path_selected (GiggleDiffTreeView *diff_tree_view,
			    const gchar        *path,
			    GiggleViewHistory  *view)
{
	GiggleViewHistoryPriv *priv;
	GList                 *list = NULL;

	priv = GET_PRIV (view);

	if (priv->current_history_elem) {
		list = priv->current_history_elem;

		if (list->prev) {
			/* unlink from the first elements */
			list->prev->next = NULL;
			list->prev = NULL;

			g_list_foreach (priv->history, (GFunc) g_free, NULL);
			g_list_free (priv->history);
		}
	} else {
		g_list_foreach (priv->history, (GFunc) g_free, NULL);
		g_list_free (priv->history);
	}

	list = g_list_prepend (list, g_strdup (path));
	priv->history = priv->current_history_elem = list;

	view_history_update_revisions (view);
}

GtkWidget *
giggle_view_history_new (void)
{
	return g_object_new (GIGGLE_TYPE_VIEW_HISTORY, NULL);
}

void
giggle_view_history_set_compact_mode (GiggleViewHistory *view,
				      gboolean           compact_mode)
{
	GiggleViewHistoryPriv *priv;

	g_return_if_fail (GIGGLE_IS_VIEW_HISTORY (view));

	priv = GET_PRIV (view);

	giggle_rev_list_view_set_compact_mode (GIGGLE_REV_LIST_VIEW (priv->revision_list), compact_mode);
	giggle_diff_view_set_compact_mode (GIGGLE_DIFF_VIEW (priv->diff_view), compact_mode);
	giggle_revision_view_set_compact_mode (GIGGLE_REVISION_VIEW (priv->revision_view), compact_mode);
	giggle_diff_tree_view_set_compact_mode (GIGGLE_DIFF_TREE_VIEW (priv->diff_tree_view), compact_mode);

	priv->compact_mode = compact_mode;
}

gboolean
giggle_view_history_get_compact_mode  (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;

	g_return_val_if_fail (GIGGLE_IS_VIEW_HISTORY (view), FALSE);

	priv = GET_PRIV (view);
	return priv->compact_mode;
}

void
giggle_view_history_set_graph_visible (GiggleViewHistory *view,
				       gboolean           visible)
{
	GiggleViewHistoryPriv *priv;

	g_return_if_fail (GIGGLE_IS_VIEW_HISTORY (view));

	priv = GET_PRIV (view);

	giggle_rev_list_view_set_graph_visible (
		GIGGLE_REV_LIST_VIEW (priv->revision_list), visible);
}

static void
view_history_go_back (GiggleHistory *history)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (history);

	if (priv->current_history_elem) {
		priv->current_history_elem = priv->current_history_elem->next;
		giggle_history_changed (history);
		view_history_update_revisions (GIGGLE_VIEW_HISTORY (history));
	}
}

static gboolean
view_history_can_go_back (GiggleHistory *history)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (history);

	return (priv->current_history_elem != NULL ||
		g_list_length (priv->history) > 2);
}

static void
view_history_go_forward (GiggleHistory *history)
{
	GiggleViewHistoryPriv *priv;
	gboolean               changed = FALSE;

	priv = GET_PRIV (history);

	if (!priv->current_history_elem) {
		/* go to the last one in list (first in history) */
		priv->current_history_elem = g_list_last (priv->history);
		changed = TRUE;
	} else if (priv->current_history_elem != priv->history) {
		priv->current_history_elem = priv->current_history_elem->prev;
		changed = TRUE;
	}

	if (changed) {
		view_history_update_revisions (GIGGLE_VIEW_HISTORY (history));
		giggle_history_changed (history);
	}
}

static gboolean
view_history_can_go_forward (GiggleHistory *history)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (history);

	return (priv->current_history_elem != priv->history ||
		g_list_length (priv->history) > 2);
}
