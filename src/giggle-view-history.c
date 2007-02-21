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

#include "giggle-view-history.h"
#include "giggle-file-list.h"
#include "giggle-revision-list.h"
#include "giggle-revision-view.h"
#include "giggle-diff-view.h"

typedef struct GiggleViewHistoryPriv GiggleViewHistoryPriv;

struct GiggleViewHistoryPriv {
	GtkWidget *file_list;
	GtkWidget *revision_list;
	GtkWidget *revision_view;
	GtkWidget *diff_view;
};

static void    view_history_finalize              (GObject *object);

static void    view_history_revision_list_selection_changed_cb (GiggleRevisionList *list,
								GiggleRevision     *revision1,
								GiggleRevision     *revision2,
								GiggleViewHistory  *view);


G_DEFINE_TYPE (GiggleViewHistory, giggle_view_history, GIGGLE_TYPE_VIEW)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GIGGLE_TYPE_VIEW_HISTORY, GiggleViewHistoryPriv))

static void
giggle_view_history_class_init (GiggleViewHistoryClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = view_history_finalize;

	g_type_class_add_private (object_class, sizeof (GiggleViewHistoryPriv));
}

static void
giggle_view_history_init (GiggleViewHistory *view)
{
	GiggleViewHistoryPriv *priv;
	GtkWidget             *hpaned, *vpaned, *vbox;
	GtkWidget             *scrolled_window;
	GtkWidget             *expander;

	priv = GET_PRIV (view);

	gtk_widget_push_composite_child ();

	hpaned = gtk_hpaned_new ();
	gtk_widget_show (hpaned);
	gtk_container_add (GTK_CONTAINER (view), hpaned);

	vpaned = gtk_vpaned_new ();
	gtk_widget_show (vpaned);
	gtk_paned_pack2 (GTK_PANED (hpaned), vpaned, TRUE, FALSE);

	/* FIXME: hardcoded sizes are evil */
	gtk_paned_set_position (GTK_PANED (hpaned), 150);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_paned_pack2 (GTK_PANED (vpaned), vbox, FALSE, FALSE);

	/* file view */
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);

	priv->file_list = giggle_file_list_new ();
	gtk_container_add (GTK_CONTAINER (scrolled_window), priv->file_list);
	gtk_widget_show_all (scrolled_window);

	gtk_paned_pack1 (GTK_PANED (hpaned), scrolled_window, FALSE, FALSE);

	/* revisions list */
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);

	priv->revision_list = giggle_revision_list_new ();
	g_signal_connect (G_OBJECT (priv->revision_list), "selection-changed",
			  G_CALLBACK (view_history_revision_list_selection_changed_cb), view);

	gtk_container_add (GTK_CONTAINER (scrolled_window), priv->revision_list);
	gtk_widget_show_all (scrolled_window);

	gtk_paned_pack1 (GTK_PANED (vpaned), scrolled_window, TRUE, FALSE);

	/* revision view */
	expander = gtk_expander_new_with_mnemonic (_("Revision _information"));
	priv->revision_view = giggle_revision_view_new ();
	gtk_container_add (GTK_CONTAINER (expander), priv->revision_view);
	gtk_widget_show_all (expander);

	gtk_box_pack_start (GTK_BOX (vbox), expander, FALSE, TRUE, 0);

	/* diff view */
	expander = gtk_expander_new_with_mnemonic (_("_Differences"));

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);

	priv->diff_view = giggle_diff_view_new ();

	gtk_container_add (GTK_CONTAINER (scrolled_window), priv->diff_view);
	gtk_container_add (GTK_CONTAINER (expander), scrolled_window);
	gtk_widget_show_all (expander);

	gtk_box_pack_start (GTK_BOX (vbox), expander, TRUE, TRUE, 0);

	gtk_widget_pop_composite_child ();
}

static void
view_history_finalize (GObject *object)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (object);
}

static void
view_history_revision_list_selection_changed_cb (GiggleRevisionList *list,
						 GiggleRevision     *revision1,
						 GiggleRevision     *revision2,
						 GiggleViewHistory  *view)
{
	GiggleViewHistoryPriv *priv;

	priv = GET_PRIV (view);

	giggle_revision_view_set_revision (
		GIGGLE_REVISION_VIEW (priv->revision_view), revision1);

	if (revision1 && revision2) {
		giggle_diff_view_set_revisions (GIGGLE_DIFF_VIEW (priv->diff_view),
						revision1, revision2);

		/* FIXME
		priv->current_diff_tree_job = giggle_git_diff_tree_new (revision2, revision1);
		giggle_git_run_job (priv->git,
				    priv->current_diff_tree_job,
				    window_git_diff_tree_result_callback,
				    window);
		*/
	}

	g_object_unref (revision1);
	g_object_unref (revision2);
}


GtkWidget *
giggle_view_history_new (void)
{
	return g_object_new (GIGGLE_TYPE_VIEW_HISTORY, NULL);
}

/* FIXME: this function is ugly, but we somehow want
 * the revisions list to be global to all the application */
void
giggle_view_history_set_model (GiggleViewHistory *view_history,
			       GtkTreeModel      *model)
{
	GiggleViewHistoryPriv *priv;

	g_return_if_fail (GIGGLE_IS_VIEW_HISTORY (view_history));
	g_return_if_fail (GTK_IS_TREE_MODEL (model));

	priv = GET_PRIV (view_history);

	giggle_revision_list_set_model (GIGGLE_REVISION_LIST (priv->revision_list), model);
}
