/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *
 * vim: ts=8 sw=8 noet
 */
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

#include "eggfindbar.h"
#include "giggle-clone-dialog.h"
#include "giggle-diff-window.h"
#include "giggle-helpers.h"
#include "giggle-view-file.h"
#include "giggle-view-history.h"
#include "giggle-view-summary.h"

#include <libgiggle/giggle-clipboard.h>
#include <libgiggle/giggle-history.h>
#include <libgiggle/giggle-plugin-manager.h>
#include <libgiggle/giggle-searchable.h>
#include <libgiggle/giggle-view-shell.h>

#include <libgiggle-git/giggle-git-config.h>

#ifdef GDK_WINDOWING_QUARTZ
#include "ige-mac-menu.h"
#endif

#include <glib/gi18n.h>
#include <string.h>
#include <stdlib.h>


#define RECENT_REPOS_PLACEHOLDER_PATH	"/ui/MainMenubar/ProjectMenu/RecentRepositories"

#define HISTORY_GO_BACK_PATH		"/ui/MainToolbar/HistoryGoBack"
#define HISTORY_GO_FORWARD_PATH		"/ui/MainToolbar/HistoryGoForward"
#if 0
#define SAVE_PATCH_UI_PATH 		"/ui/MainMenubar/ProjectMenu/SavePatch"
#endif
#define SHOW_GRAPH_PATH			"/ui/MainMenubar/ViewMenu/ShowGraph"

#define CUT_PATH			"/ui/MainMenubar/EditMenu/Cut"
#define COPY_PATH			"/ui/MainMenubar/EditMenu/Copy"
#define PASTE_PATH			"/ui/MainMenubar/EditMenu/Paste"
#define DELETE_PATH			"/ui/MainMenubar/EditMenu/Delete"

#define MAX_N_RECENT			10
#define RECENT_ITEM_MAX_N_CHARS		40

enum {
	SEARCH_NEXT,
	SEARCH_PREV
};

struct _GiggleWindowPriv
{
	/* Model */
	GiggleGit           *git;
	GiggleGitConfig     *configuration;
	GtkUIManager        *ui_manager;

	gint                 width;
	gint                 height;
	gint                 x;
	gint                 y;

	/* Widgets */
	GtkWidget           *content_vbox;
	GtkWidget           *view_shell;

	GtkWidget           *find_bar;
	GtkToolItem         *full_search;

	/* Views */
	GtkWidget           *file_view;
	GtkWidget           *history_view;

	/* Dialogs */
	GtkWidget           *summary_dialog;
	GtkWidget           *personal_details_window;

	/* Recent File Manager */
	GtkRecentManager    *recent_manager;
	GtkActionGroup      *recent_action_group;
	guint                recent_merge_id;

#if 0
	GtkWidget           *diff_current_window;
#endif

	GiggleClipboard     *clipboard;
	GigglePluginManager *plugin_manager;

	GList               *history;
	guint                history_locked;
};

typedef struct {
	GiggleView *view;
	GObject    *view_data;
} GiggleWindowSnapshot;

static void giggle_window_clipboard_init (GiggleClipboardIface *iface);

G_DEFINE_TYPE_WITH_CODE (GiggleWindow, giggle_window, GTK_TYPE_WINDOW,
			 G_IMPLEMENT_INTERFACE (GIGGLE_TYPE_CLIPBOARD,
						giggle_window_clipboard_init))


static void
window_history_list_free (GList *list)
{
	GiggleWindowSnapshot *snapshot;

	while (list) {
		snapshot = list->data;

		if (snapshot->view_data)
			g_object_unref (snapshot->view_data);

		g_slice_free (GiggleWindowSnapshot, snapshot);
		list = g_list_delete_link (list, list);
	}
}

static void
window_history_update_ui (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	gboolean          back = FALSE, forward = FALSE;
	GtkAction        *action;

	priv = window->priv;

	if (!priv->ui_manager)
		return;

	if (priv->history) {
		back = (NULL != priv->history->prev);
		forward = (NULL != priv->history->next);
	}

	action = gtk_ui_manager_get_action (priv->ui_manager, HISTORY_GO_BACK_PATH);
	gtk_action_set_sensitive (action, back);

	action = gtk_ui_manager_get_action (priv->ui_manager, HISTORY_GO_FORWARD_PATH);
	gtk_action_set_sensitive (action, forward);
}

static void
window_history_reset (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	GList            *head;

	priv = window->priv;

	head = g_list_first (priv->history);
	window_history_list_free (head);
	priv->history = NULL;

	window_history_update_ui (window);
}

static void
window_history_remove_view (GiggleWindow *window,
			    GiggleView   *view)
{
	window_history_update_ui (window);
}

static void
window_history_capture (GiggleWindow *window)
{
	GiggleWindowPriv     *priv;
	GiggleWindowSnapshot *snapshot;

	priv = window->priv;

	if (priv->history_locked)
		return;

	snapshot = g_slice_new0 (GiggleWindowSnapshot);

	snapshot->view = giggle_view_shell_get_selected
		(GIGGLE_VIEW_SHELL (priv->view_shell));

	if (GIGGLE_IS_HISTORY (snapshot->view)) {
		snapshot->view_data = giggle_history_capture
			(GIGGLE_HISTORY (snapshot->view));
	}

	if (priv->history) {
		window_history_list_free (priv->history->next);
		priv->history->next = NULL;
	}

	priv->history = g_list_append (priv->history, snapshot);
	priv->history = g_list_last (priv->history);

	window_history_update_ui (window);
}

static void
window_history_restore_snapshot (GiggleWindow         *window,
				 GiggleWindowSnapshot *snapshot)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	if (priv->history_locked)
		return;

	priv->history_locked += 1;

	giggle_view_shell_select (GIGGLE_VIEW_SHELL (priv->view_shell),
				  snapshot->view);

	if (GIGGLE_IS_HISTORY (snapshot->view)) {
		giggle_history_restore (GIGGLE_HISTORY (snapshot->view),
					snapshot->view_data);
	}

	window_history_update_ui (window);

	priv->history_locked -= 1;
}

static void
window_dispose (GObject *object)
{
	GiggleWindow     *window;
	GiggleWindowPriv *priv;

	window = GIGGLE_WINDOW (object);
	priv = window->priv;

	if (priv->view_shell) {
		while (gtk_notebook_get_n_pages (GTK_NOTEBOOK (priv->view_shell)))
			gtk_notebook_remove_page (GTK_NOTEBOOK (priv->view_shell), 0);

		priv->view_shell = NULL;
	}

	window_history_reset (window);

	if (priv->summary_dialog) {
		gtk_widget_destroy (priv->summary_dialog);
		priv->summary_dialog = NULL;
	}

	if (priv->personal_details_window) {
		gtk_widget_destroy (priv->personal_details_window);
		priv->personal_details_window = NULL;
	}

	if (priv->git) {
		g_object_unref (priv->git);
		priv->git = NULL;
	}

	if (priv->recent_manager) {
		g_object_unref (priv->recent_manager);
		priv->recent_manager = NULL;
	}

	if (priv->recent_action_group) {
		gtk_ui_manager_remove_action_group (priv->ui_manager,
						    priv->recent_action_group);
		priv->recent_action_group = NULL;
	}

	if (priv->ui_manager) {
		g_object_unref (priv->ui_manager);
		priv->ui_manager = NULL;
	}

	if (priv->configuration) {
		g_object_unref (priv->configuration);
		priv->configuration = NULL;
	}

	if (priv->plugin_manager) {
		g_object_unref (priv->plugin_manager);
		priv->plugin_manager = NULL;
	}

	G_OBJECT_CLASS (giggle_window_parent_class)->dispose (object);
}

static void
window_configuration_committed_cb (GiggleGitConfig *configuration,
				   gboolean         success,
				   gpointer         user_data)
{
	gtk_main_quit ();
}

static void
window_save_state (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	char              geometry[25];
	gboolean          maximized;

	priv = window->priv;

	g_snprintf (geometry, sizeof (geometry), "%dx%d+%d+%d",
		    priv->width, priv->height, priv->x, priv->y);

	maximized = gdk_window_get_state (gtk_widget_get_window (GTK_WIDGET (window))) & GDK_WINDOW_STATE_MAXIMIZED;

	giggle_git_config_set_field (priv->configuration,
				     GIGGLE_GIT_CONFIG_FIELD_MAIN_WINDOW_GEOMETRY,
				     geometry);

	giggle_git_config_set_boolean_field (priv->configuration,
					     GIGGLE_GIT_CONFIG_FIELD_MAIN_WINDOW_MAXIMIZED,
					     maximized);

	giggle_git_config_commit (priv->configuration,
				  window_configuration_committed_cb,
				  window);
}

static gboolean
window_configure_event (GtkWidget         *widget,
			GdkEventConfigure *event)
{
	GiggleWindowPriv *priv;

	priv = GIGGLE_WINDOW (widget)->priv;

	if (!(gdk_window_get_state (gtk_widget_get_window (widget)) & GDK_WINDOW_STATE_MAXIMIZED)) {
		gtk_window_get_size (GTK_WINDOW (widget), &priv->width, &priv->height);
		gtk_window_get_position (GTK_WINDOW (widget), &priv->x, &priv->y);
	}

	return GTK_WIDGET_CLASS (giggle_window_parent_class)->configure_event (widget, event);
}

static gboolean
window_delete_event (GtkWidget   *widget,
		     GdkEventAny *event)
{
	window_save_state (GIGGLE_WINDOW (widget));
	gtk_widget_hide (widget);

	return TRUE;
}

static void
window_clipboard_changed_cb (GiggleClipboard *clipboard,
			     GiggleWindow    *window)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	if (!priv->ui_manager)
		return;

	gtk_action_set_sensitive (gtk_ui_manager_get_action (priv->ui_manager, CUT_PATH),
			          clipboard && giggle_clipboard_can_cut (clipboard));
	gtk_action_set_sensitive (gtk_ui_manager_get_action (priv->ui_manager, COPY_PATH),
			          clipboard && giggle_clipboard_can_copy (clipboard));
	gtk_action_set_sensitive (gtk_ui_manager_get_action (priv->ui_manager, PASTE_PATH),
			          clipboard && giggle_clipboard_can_paste (clipboard));
	gtk_action_set_sensitive (gtk_ui_manager_get_action (priv->ui_manager, DELETE_PATH),
			          clipboard && giggle_clipboard_can_delete (clipboard));
}

static GiggleClipboard *
window_find_clipboard (GiggleWindow *window)
{
	GtkWidget *child;

	child = gtk_window_get_focus (GTK_WINDOW (window));

	if (child && !GIGGLE_IS_CLIPBOARD (child))
		child = gtk_widget_get_ancestor (child, GIGGLE_TYPE_CLIPBOARD);

	if (child)
		return GIGGLE_CLIPBOARD (child);

	return NULL;
}

static void
window_set_focus (GtkWindow *window,
		  GtkWidget *widget)
{
	GiggleWindowPriv *priv;
	gpointer          clipboard_proxy;
	GiggleClipboard  *clipboard;
	GtkTextBuffer    *buffer;

	priv = GIGGLE_WINDOW (window)->priv;

	clipboard_proxy = gtk_window_get_focus (window);

	if (GTK_IS_TEXT_VIEW (clipboard_proxy))
		clipboard_proxy = gtk_text_view_get_buffer (clipboard_proxy);

	if (clipboard_proxy) {
		g_signal_handlers_disconnect_by_func (clipboard_proxy,
						      giggle_clipboard_changed, window);
	}

	GTK_WINDOW_CLASS (giggle_window_parent_class)->set_focus (window, widget);

	clipboard = window_find_clipboard (GIGGLE_WINDOW (window));

	if (GTK_IS_LABEL (widget)) {
		g_signal_connect_swapped (widget, "notify::cursor-position",
					  G_CALLBACK (giggle_clipboard_changed),
					  window);
		g_signal_connect_swapped (widget, "notify::selection-bound",
					  G_CALLBACK (giggle_clipboard_changed),
					  window);
	} else if (GTK_IS_TEXT_VIEW (widget)) {
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));

		g_signal_connect_swapped (buffer, "notify::has-selection",
					  G_CALLBACK (giggle_clipboard_changed),
					  window);
	}

	if (clipboard != priv->clipboard) {
		if (priv->clipboard) {
			g_signal_handlers_disconnect_by_func (priv->clipboard,
							      window_clipboard_changed_cb,
							      window);

			priv->clipboard = NULL;
		}

		priv->clipboard = clipboard;

		if (priv->clipboard) {
			g_signal_connect (priv->clipboard, "clipboard-changed",
					  G_CALLBACK (window_clipboard_changed_cb),
					  window);
		}
	}

	window_clipboard_changed_cb (priv->clipboard, GIGGLE_WINDOW (window));
}

static void
giggle_window_class_init (GiggleWindowClass *class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	GtkWindowClass *window_class = GTK_WINDOW_CLASS (class);

	object_class->dispose         = window_dispose;
	widget_class->configure_event = window_configure_event;
	widget_class->delete_event    = window_delete_event;
	window_class->set_focus       = window_set_focus;

	g_type_class_add_private (class, sizeof (GiggleWindowPriv));
}

static gboolean
window_can_copy (GiggleClipboard *clipboard)
{
	GtkTextBuffer *buffer;
	GtkWidget     *focus;

	focus = gtk_window_get_focus (GTK_WINDOW (clipboard));

	if (GTK_IS_LABEL (focus))
		return gtk_label_get_selection_bounds (GTK_LABEL (focus), NULL, NULL);

	if (GTK_IS_TEXT_VIEW (focus)) {
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (focus));
		return gtk_text_buffer_get_selection_bounds (buffer, NULL, NULL);
	}

	return FALSE;
}

static void
window_do_copy (GiggleClipboard *clipboard)
{
	GtkWidget    *focus = gtk_window_get_focus (GTK_WINDOW (clipboard));
	GtkClipboard *widget_clipboard;

	widget_clipboard = gtk_widget_get_clipboard (focus, GDK_SELECTION_CLIPBOARD);

	if (GTK_IS_LABEL (focus)) {
		int         start_offset, end_offset;
		char       *begin, *end;
		const char *text;

		if (gtk_label_get_selection_bounds (GTK_LABEL (focus),
						    &start_offset, &end_offset)) {
			text = gtk_label_get_text (GTK_LABEL (focus));
			begin = g_utf8_offset_to_pointer (text, start_offset);
			end = g_utf8_offset_to_pointer (text, end_offset);

			if (end > begin) {
				gtk_clipboard_set_text (widget_clipboard,
							begin, end - begin);
			}
		}
	} else if (GTK_IS_TEXT_VIEW (focus)) {
		GtkTextIter    start, end;
		GtkTextBuffer *buffer;
		char          *text;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (focus));

		if (gtk_text_buffer_get_selection_bounds (buffer, &start, &end)) {
			text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
			gtk_clipboard_set_text (widget_clipboard, text, -1);
			g_free (text);
		}
	}
}

static void
giggle_window_clipboard_init (GiggleClipboardIface *iface)
{
	iface->can_copy = window_can_copy;
	iface->do_copy  = window_do_copy;
}

#if 0
static void
window_create_menu (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	GtkActionGroup   *action_group;
	GError           *error = NULL;

	priv = window->priv;;


#ifdef GDK_WINDOWING_QUARTZ
	{
		GtkWidget       *menu;
		GtkWidget       *item;
		IgeMacMenuGroup *group;

		menu = gtk_ui_manager_get_widget (priv->ui_manager,
						  "/MainMenubar");

		ige_mac_menu_set_menu_bar (GTK_MENU_SHELL (menu));
		gtk_widget_hide (menu);

		item = gtk_ui_manager_get_widget (priv->ui_manager,
						  "/MainMenubar/ProjectMenu/Quit");
		ige_mac_menu_set_quit_menu_item (GTK_MENU_ITEM (item));

		item = gtk_ui_manager_get_widget (priv->ui_manager,
						  "/MainMenubar/HelpMenu/About");
		group =  ige_mac_menu_add_app_menu_group ();
		ige_mac_menu_add_app_menu_item  (group, GTK_MENU_ITEM (item),
						 _("About Giggle"));
	}
#endif
}

#endif

void
giggle_window_set_directory (GiggleWindow *window,
			     const gchar  *directory)
{
	GiggleWindowPriv *priv;
	GError           *error = NULL;

	priv = window->priv;

	if (!giggle_git_set_directory (priv->git, directory, &error)) {
		GtkWidget *dialog;

		gtk_widget_hide (priv->view_shell);

		dialog = gtk_message_dialog_new (GTK_WINDOW (window),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("The directory '%s' does not look like a "
						   "git repository."), directory);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	} else {
		gtk_widget_show (priv->view_shell);
	}
}

static void
window_bind_state (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	const char       *geometry;
	gboolean	  maximized;
	GtkAction 	 *show_graph_action;

	priv = window->priv;

	if (!gtk_widget_get_visible (GTK_WIDGET (window))) {
		geometry = giggle_git_config_get_field (priv->configuration,
							GIGGLE_GIT_CONFIG_FIELD_MAIN_WINDOW_GEOMETRY);
		maximized = giggle_git_config_get_boolean_field (priv->configuration,
								 GIGGLE_GIT_CONFIG_FIELD_MAIN_WINDOW_MAXIMIZED);

		if (!geometry) {
			gtk_window_set_default_size (GTK_WINDOW (window), 700, 550);
		} else {
			if (!gtk_window_parse_geometry (GTK_WINDOW (window), geometry))
				geometry = NULL;
		}

		if (maximized) {
			gtk_window_maximize (GTK_WINDOW (window));
		}
	}

	show_graph_action = gtk_ui_manager_get_action
		(priv->ui_manager, SHOW_GRAPH_PATH);

	giggle_git_config_bind (priv->configuration,
				GIGGLE_GIT_CONFIG_FIELD_SHOW_GRAPH,
				G_OBJECT (show_graph_action), "active");
	giggle_git_config_bind (priv->configuration,
				GIGGLE_GIT_CONFIG_FIELD_MAIN_WINDOW_VIEW,
				G_OBJECT (priv->view_shell), "view-name");
	giggle_git_config_bind (priv->configuration,
				GIGGLE_GIT_CONFIG_FIELD_FILE_VIEW_PATH,
				G_OBJECT (priv->file_view), "path");

	window_history_reset (window);
	window_history_capture (window);

	gtk_widget_show (GTK_WIDGET (window));

#if 0
       if (priv->diff_current_window) {
               gtk_widget_show (priv->diff_current_window);
       }
#endif
}

static void
configuration_updated_cb (GiggleGitConfig *configuration,
			  gboolean         success,
			  gpointer         user_data)
{
	if (success)
		window_bind_state (GIGGLE_WINDOW (user_data));
}

static void
window_action_open_cb (GtkAction    *action,
		       GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	GtkWidget        *file_chooser;
	gchar      	 *directory = NULL;

	priv = window->priv;

	file_chooser = gtk_file_chooser_dialog_new (
		_("Select git repository"),
		GTK_WINDOW (window),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);

	gtk_file_chooser_set_current_folder (
		GTK_FILE_CHOOSER (file_chooser),
		giggle_git_get_directory (priv->git));

	if (gtk_dialog_run (GTK_DIALOG (file_chooser)) == GTK_RESPONSE_OK) {
		directory = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (file_chooser));
	}

	gtk_widget_destroy (file_chooser);

	if (directory) {
		giggle_window_set_directory (window, directory);
		g_free (directory);
	}
}

static void
window_action_clone_cb (GtkAction    *action,
		       GiggleWindow *window)
{
	GtkWidget *clone_dialog;

	clone_dialog = giggle_clone_dialog_new (NULL, NULL);
	gtk_window_set_transient_for (GTK_WINDOW (clone_dialog),
	                              GTK_WINDOW (window));
	if (gtk_dialog_run (GTK_DIALOG (clone_dialog)) == GTK_RESPONSE_ACCEPT)
		giggle_window_set_directory (window,
				giggle_clone_dialog_get_directory (
					GIGGLE_CLONE_DIALOG (clone_dialog)
				));
	gtk_widget_destroy (clone_dialog);
}

static void
window_action_properties_cb (GtkAction    *action,
		             GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	char		 *title;
	const gchar      *project_name;
	GtkWidget	 *summary_view;

	priv = window->priv;

	if (!priv->summary_dialog) {
		project_name = giggle_git_get_project_name (priv->git);
		title = g_strdup_printf (_("Properties of %s"), project_name);

		priv->summary_dialog = gtk_dialog_new_with_buttons
			(title, GTK_WINDOW (window),
			 GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
			 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);

		summary_view = giggle_view_summary_new ();

		gtk_box_pack_start
			(GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (priv->summary_dialog))),
			 summary_view, TRUE, TRUE, 0);

		gtk_window_set_default_size
			(GTK_WINDOW (priv->summary_dialog), 460, 400);

		gtk_widget_show (summary_view);

		g_free (title);
	}

	gtk_dialog_run (GTK_DIALOG (priv->summary_dialog));
	gtk_widget_hide (priv->summary_dialog);
}

static void
window_action_quit_cb (GtkAction    *action,
		       GiggleWindow *window)
{
	window_save_state (window);
	gtk_widget_hide (GTK_WIDGET (window));
}

static void
window_action_cut_cb (GtkAction    *action,
		      GiggleWindow *window)
{
	GiggleClipboard *clipboard = window_find_clipboard (window);

	if (clipboard && giggle_clipboard_can_cut (clipboard))
		giggle_clipboard_cut (clipboard);
}

static void
window_action_copy_cb (GtkAction    *action,
		      GiggleWindow *window)
{
	GiggleClipboard *clipboard = window_find_clipboard (window);

	if (clipboard && giggle_clipboard_can_copy (clipboard))
		giggle_clipboard_copy (clipboard);
}

static void
window_action_paste_cb (GtkAction    *action,
		      GiggleWindow *window)
{
	GiggleClipboard *clipboard = window_find_clipboard (window);

	if (clipboard && giggle_clipboard_can_paste (clipboard))
		giggle_clipboard_paste (clipboard);
}

static void
window_action_delete_cb (GtkAction    *action,
		      GiggleWindow *window)
{
	GiggleClipboard *clipboard = window_find_clipboard (window);

	if (clipboard && giggle_clipboard_can_delete (clipboard))
		giggle_clipboard_delete (clipboard);
}

static void
window_find (EggFindBar            *find_bar,
	     GiggleWindow          *window,
	     GiggleSearchDirection  direction)
{
	GiggleWindowPriv *priv;
	GiggleView       *view;
	const gchar      *search_string;
	gboolean          full_search;

	priv = window->priv;

	view = giggle_view_shell_get_selected (GIGGLE_VIEW_SHELL (priv->view_shell));

	g_return_if_fail (GIGGLE_IS_SEARCHABLE (view));

	search_string = egg_find_bar_get_search_string (find_bar);

	if (search_string && *search_string) {
		full_search = gtk_toggle_tool_button_get_active (
			GTK_TOGGLE_TOOL_BUTTON (priv->full_search));

		giggle_searchable_search (GIGGLE_SEARCHABLE (view),
					  search_string, direction, full_search);
	}
}

static void
window_find_next (EggFindBar   *find_bar,
		  GiggleWindow *window)
{
	window_find (find_bar, window, GIGGLE_SEARCH_DIRECTION_NEXT);
}

static void
window_find_previous (EggFindBar   *find_bar,
		      GiggleWindow *window)
{
	window_find (find_bar, window, GIGGLE_SEARCH_DIRECTION_PREV);
}

static void
window_action_find_cb (GtkAction    *action,
		       GiggleWindow *window)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	gtk_widget_show (priv->find_bar);
	gtk_widget_grab_focus (priv->find_bar);
}

static void
window_action_find_next_cb (GtkAction    *action,
			    GiggleWindow *window)
{
	window_find_next (EGG_FIND_BAR (window->priv->find_bar), window);
}

static void
window_action_find_prev_cb (GtkAction    *action,
			    GiggleWindow *window)
{
	window_find_previous (EGG_FIND_BAR (window->priv->find_bar), window);
}

static void
window_action_view_graph_cb (GtkAction    *action,
			     GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	gboolean          active;

	priv = window->priv;

	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	giggle_view_file_set_graph_visible (GIGGLE_VIEW_FILE (priv->file_view), active);
	giggle_view_history_set_graph_visible (GIGGLE_VIEW_HISTORY (priv->history_view), active);
}

static void
window_action_refresh_history (GtkAction    *action,
			       GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	const gchar      *directory;

	priv = window->priv;

	directory = giggle_git_get_directory (priv->git);
	giggle_window_set_directory (window, directory);
}

static void
window_visit_uri (GiggleWindow *window,
		  const char   *uri)
{
	GError            *error = NULL;

	gtk_show_uri (gtk_widget_get_screen (GTK_WIDGET (window)),
	              uri,  gtk_get_current_event_time (),
	              &error);

	if (error != NULL) {
		giggle_error_dialog (GTK_WINDOW (window), error);
		g_clear_error (&error);
	}
}

static void
window_action_help_index_cb (GtkAction    *action,
                             GiggleWindow *window)
{
	window_visit_uri (window, "ghelp:" PACKAGE_TARNAME);
}

static void
window_action_bug_report_cb (GtkAction    *action,
			     GiggleWindow *window)
{
	window_visit_uri (window, PACKAGE_BUGREPORT);
}

static void
window_action_about_cb (GtkAction    *action,
			GiggleWindow *window)
{
	gchar *license_translated;

	const char *artists[] = {
		"Andreas Nilsson",
		NULL
	};

	const gchar *authors[] = {
		"Carlos Garnacho",
		"Mathias Hasselmann",
		"Mikael Hallendal",
		"Richard Hult",
		"Sven Herzberg",
		"",
		_("Contributors:"),
		"Javier Jardón",
		"Łukasz Jernaś",
		"Guilhem Bonnefille",
		"Florian Müllner",
		NULL
	};

	const gchar *documenters[] = {
		"Łukasz Jernaś",
		NULL
	};

	const gchar *copyright = _("Copyright © 2007 - 2008 Imendio AB\n"
	                           "Copyright © 2008 - 2009 Mathias Hasselmann\n"
	                           "Copyright © 2009 - 2010 The Giggle authors");

	const gchar *license[] = {
		N_("This program is free software; you can redistribute it and/or modify "
		   "it under the terms of the GNU General Public License as published by "
		   "the Free Software Foundation; either version 2 of the License, or "
		   "(at your option) any later version."),
		N_("This program is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License "
		   "along with this program; if not, write to the Free Software Foundation, Inc., "
		   "51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA")
	};
	license_translated = g_strjoin ("\n\n",
	                                _(license[0]),
	                                _(license[1]),
	                                _(license[2]),
	                                NULL);

	/* Translators: This is a special message that shouldn't be translated
         * literally. It is used in the about box to give credits to
         * the translators.
         * Thus, you should translate it to your name and email address.
         * You should also include other translators who have contributed to
         * this translation; in that case, please write each of them on a separate
         * line seperated by newlines (\n).
         */
	const gchar *translators = _("translator-credits");

	gtk_show_about_dialog (GTK_WINDOW (window),
	                       "artists", artists,
	                       "authors", authors,
	                       "comments", _("A graphical frontend for Git"),
			       "copyright", copyright,
	                       "documenters", documenters,
	                       "license", license_translated,
	                       "logo-icon-name", PACKAGE,
	                       "title", _("About Giggle"),
			       "translator-credits", translators,
	                       "version", VERSION,
			       "website", PACKAGE_URL,
	                       "website-label", _("Giggle Website"),
	                       "wrap-license", TRUE,
			       NULL);

	g_free (license_translated);
}

static void
window_add_widget_cb (GtkUIManager *merge,
		      GtkWidget    *widget,
		      GiggleWindow *window)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	if (GTK_IS_TOOLBAR (widget))
		gtk_toolbar_set_show_arrow (GTK_TOOLBAR (widget), FALSE);

	if (g_str_has_prefix (gtk_widget_get_name (widget), "Main"))
		gtk_box_pack_start (GTK_BOX (priv->content_vbox),
				    widget, FALSE, FALSE, 0);
}

static void
window_action_history_go_back (GtkAction    *action,
			       GiggleWindow *window)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	g_return_if_fail (priv->history);
	g_return_if_fail (priv->history->prev);

	priv->history = priv->history->prev;
	window_history_restore_snapshot (window, priv->history->data);
}

static void
window_action_history_go_forward (GtkAction    *action,
				  GiggleWindow *window)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	g_return_if_fail (priv->history);
	g_return_if_fail (priv->history->next);

	priv->history = priv->history->next;
	window_history_restore_snapshot (window, priv->history->data);
}

static void
window_create_ui_manager (GiggleWindow *window)
{
	static const GtkActionEntry find_action_entries[] = {
		{ "Find", GTK_STOCK_FIND, N_("_Find..."),
		  "<control>F", N_("Find..."),
		  G_CALLBACK (window_action_find_cb)
		},
		{ "FindSlash", NULL, NULL, "slash", NULL,
		  G_CALLBACK (window_action_find_cb)
		},
		{ "FindNext", NULL, N_("Find Ne_xt"),
		  "<control>G", N_("Find next match"),
		  G_CALLBACK (window_action_find_next_cb)
		},
		{ "FindPrev", NULL, N_("Find Pre_vious"),
		  "<control><shift>G", N_("Find previous match"),
		  G_CALLBACK (window_action_find_prev_cb)
		},
	};

	static const GtkActionEntry action_entries[] = {
		{ "ProjectMenu", NULL, N_("_Project"), NULL, NULL, NULL },
		{ "EditMenu",    NULL, N_("_Edit"),    NULL, NULL, NULL },
		{ "GoMenu",      NULL, N_("_Go"),      NULL, NULL, NULL },
		{ "ViewMenu",    NULL, N_("_View"),    NULL, NULL, NULL },
		{ "HelpMenu",    NULL, N_("_Help"),    NULL, NULL, NULL },

		{ "Open", GTK_STOCK_OPEN, NULL,
		  NULL, N_("Open a git repository"),
		  G_CALLBACK (window_action_open_cb)
		},
		{ "Clone", NULL,
		  N_("Clone _location"), "<control>L", N_("Clone a location"),
		  G_CALLBACK (window_action_clone_cb)
		},
#if 0
		{ "SavePatch", GTK_STOCK_SAVE,
		  N_("_Save patch"), "<control>S", N_("Save a patch"),
		  G_CALLBACK (window_action_save_patch_cb)
		},
		{ "Diff", NULL,
		  N_("_Diff current changes"), "<control>D", N_("Diff current changes"),
		  G_CALLBACK (window_action_diff_cb)
		},
#endif
		{ "Quit", GTK_STOCK_QUIT, NULL,
		  "<control>Q", N_("Quit the application"),
		  G_CALLBACK (window_action_quit_cb)
		},

		{ "Cut", GTK_STOCK_CUT, NULL, NULL, NULL,
		  G_CALLBACK (window_action_cut_cb)
		},
		{ "Copy", GTK_STOCK_COPY, NULL, NULL, NULL,
		  G_CALLBACK (window_action_copy_cb)
		},
		{ "Paste", GTK_STOCK_PASTE, NULL, NULL, NULL,
		  G_CALLBACK (window_action_paste_cb)
		},
		{ "Delete", GTK_STOCK_DELETE, NULL, NULL, NULL,
		  G_CALLBACK (window_action_delete_cb)
		},

		{ "HistoryGoBack", GTK_STOCK_GO_BACK, NULL,
		  "<alt>Left", N_("Go backward in history"),
		  G_CALLBACK (window_action_history_go_back)
		},
		{ "HistoryGoForward", GTK_STOCK_GO_FORWARD, NULL,
		  "<alt>Right", N_("Go forward in history"),
		  G_CALLBACK (window_action_history_go_forward)
		},

		{ "HelpIndex", "help-contents", N_("_Contents"),
		  "F1", N_("Show user guide for Giggle"),
		  G_CALLBACK (window_action_help_index_cb)
		},
		{ "BugReport", NULL, N_("Report _Issue"),
		  NULL, N_("Report an issue you've found in Giggle"),
		  G_CALLBACK (window_action_bug_report_cb)
		},
		{ "About", GTK_STOCK_ABOUT, NULL,
		  NULL, N_("About this application"),
		  G_CALLBACK (window_action_about_cb)
		},
	};

	static const GtkToggleActionEntry toggle_action_entries[] = {
		{ "ShowGraph", NULL,
		  N_("Show revision tree"), "F12", NULL,
		  G_CALLBACK (window_action_view_graph_cb), TRUE
		},
	};

	static const GtkActionEntry project_action_entries[] = {
		{ "Properties", GTK_STOCK_PROPERTIES, NULL,
		  "<alt>Return", N_("Show and edit project properties"),
		  G_CALLBACK (window_action_properties_cb)
		},
		{ "RefreshHistory", GTK_STOCK_REFRESH, NULL,
		  "<control>R", N_("Refresh current view"),
		  G_CALLBACK (window_action_refresh_history)
		},
	};

	static const char layout[] =
		"<ui>"
		"  <menubar name='MainMenubar'>"
		"    <menu action='ProjectMenu'>"
		"      <menuitem action='Open'/>"
		"      <menuitem action='Clone'/>"
#if 0
		"      <menuitem action='SavePatch'/>"
		"      <menuitem action='Diff'/>"
#endif
		"      <separator/>"
		"      <menuitem action='Properties'/>"
		"      <separator/>"
		"      <placeholder name='RecentRepositories'/>"
		"      <separator/>"
		"      <menuitem action='Quit'/>"
		"    </menu>"
		"    <menu action='EditMenu'>"
		"      <menuitem action='Cut'/>"
		"      <menuitem action='Copy'/>"
		"      <menuitem action='Paste'/>"
		"      <menuitem action='Delete'/>"
		"      <separator/>"
		"      <menuitem action='Find'/>"
		"      <menuitem action='FindNext'/>"
		"      <menuitem action='FindPrev'/>"
		"      <separator/>"
		"      <placeholder name='EditMenuActions'/>"
		"      <separator/>"
		"      <placeholder name='EditMenuPreferences'/>"
		"    </menu>"
		"    <menu action='ViewMenu'>"
		"      <placeholder name='ViewShell'/>"
		"      <separator/>"
		"      <menuitem action='ShowGraph'/>"
		"      <placeholder name='ViewMenuPreferences'/>"
		"      <separator/>"
		"      <menuitem action='RefreshHistory'/>"
		"      <placeholder name='ViewMenuActions'/>"
		"    </menu>"
		"    <menu action='GoMenu'>"
		"      <menuitem action='HistoryGoBack'/>"
		"      <menuitem action='HistoryGoForward'/>"
		"    </menu>"
		"    <menu action='HelpMenu'>"
		"      <menuitem action='HelpIndex'/>"
		"      <separator/>"
		"      <menuitem action='BugReport'/>"
		"      <separator/>"
		"      <menuitem action='About'/>"
		"    </menu>"
		"  </menubar>"
		"  <toolbar name='MainToolbar'>"
		"    <toolitem action='HistoryGoBack'/>"
		"    <toolitem action='HistoryGoForward'/>"
		"    <toolitem action='RefreshHistory'/>"
		"    <separator/>"
		"    <toolitem action='Cut'/>"
		"    <toolitem action='Copy'/>"
		"    <toolitem action='Paste'/>"
		"    <separator/>"
		"    <toolitem action='Find'/>"
		"    <placeholder name='Actions' />"
		"    <separator expand='true'/>"
		"    <placeholder name='ViewShell' />"
		"  </toolbar>"
		"  <toolbar name='ViewHistoryToolbar'>"
		"    <placeholder name='Actions' />"
		"    <separator name='ViewShellSeparator' expand='true'/>"
		"    <placeholder name='ViewShell' />"
		"  </toolbar>"
		"  <accelerator action='FindSlash'/>"
		"</ui>";

	GiggleWindowPriv *priv;
	GtkActionGroup   *action_group;
	GError           *error = NULL;

	priv = window->priv;

	priv->ui_manager = gtk_ui_manager_new ();

	g_signal_connect (priv->ui_manager, "add-widget",
			  G_CALLBACK (window_add_widget_cb),
			  window);

	action_group = gtk_action_group_new ("MainActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);

	gtk_action_group_add_actions (action_group, action_entries,
				      G_N_ELEMENTS (action_entries),
				      window);

	gtk_action_group_add_toggle_actions (action_group, toggle_action_entries,
					     G_N_ELEMENTS (toggle_action_entries),
					     window);

	gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, 0);
	g_object_unref (action_group);

	action_group = gtk_action_group_new ("ProjectActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_action_group_set_sensitive (action_group, FALSE);

	gtk_action_group_add_actions (action_group, project_action_entries,
				      G_N_ELEMENTS (project_action_entries),
				      window);

	gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, 0);
	g_object_unref (action_group);

	action_group = gtk_action_group_new ("FindActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);

	gtk_action_group_add_actions (action_group, find_action_entries,
				      G_N_ELEMENTS (find_action_entries),
				      window);

	gtk_ui_manager_insert_action_group (priv->ui_manager, action_group, 0);
	g_object_unref (action_group);

	gtk_window_add_accel_group (GTK_WINDOW (window),
				    gtk_ui_manager_get_accel_group (priv->ui_manager));

	gtk_ui_manager_add_ui_from_string (priv->ui_manager,
					   layout, -1, &error);

	if (error) {
		g_error ("%s: Couldn't create UI: %s\n",
			 G_STRFUNC, error->message);
	}

	gtk_ui_manager_ensure_update (priv->ui_manager);
}

static void
window_recent_repository_activate (GtkAction    *action,
				   GiggleWindow *window)
{
	const gchar      *directory;

	directory = g_object_get_data (G_OBJECT (action), "recent-action-path");
	giggle_window_set_directory (window, directory);
}

static void
window_recent_connect_proxy_cb (GtkActionGroup *action_group,
                                GtkAction *action,
                                GtkWidget *proxy,
                                gpointer data)
{
	GtkLabel *label;

	if (!GTK_IS_MENU_ITEM (proxy))
		return;

	label = GTK_LABEL (gtk_bin_get_child (GTK_BIN (proxy)));

	gtk_label_set_ellipsize (label, PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_max_width_chars (label, RECENT_ITEM_MAX_N_CHARS);
}

static int
window_compare_recent_info (gconstpointer a,
			    gconstpointer b)
{
	GtkRecentInfo *info_a = (GtkRecentInfo *) a;
	GtkRecentInfo *info_b = (GtkRecentInfo *) b;
	time_t         time_a, time_b;

	time_a = gtk_recent_info_get_modified (info_a);
	time_b = gtk_recent_info_get_modified (info_b);

	if (time_a > time_b)
		return -1;
	if (time_a < time_b)
		return +1;

	return g_strcmp0 (gtk_recent_info_get_uri_display (info_a),
			  gtk_recent_info_get_uri_display (info_b));
}

/* this should not be necessary when there's
 * GtkRecentManager/GtkUIManager integration
 */
static void
window_recent_repositories_update (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	GList            *recent_items, *l;
	GtkRecentInfo    *info;
	GtkAction        *action;
	gchar            *action_name;
	gchar            *recent_label, *s;
	GString          *action_label;
	gint              count = 0;

	priv = window->priv;

	if (priv->recent_merge_id != 0) {
		gtk_ui_manager_remove_ui (priv->ui_manager, priv->recent_merge_id);
		priv->recent_merge_id = 0;
	}

	if (priv->recent_action_group != NULL) {
		gtk_ui_manager_remove_action_group (priv->ui_manager,
						    priv->recent_action_group);
		priv->recent_action_group = NULL;
	}

	priv->recent_action_group = gtk_action_group_new ("RecentRepositories");

	g_signal_connect
		(priv->recent_action_group, "connect-proxy",
		G_CALLBACK (window_recent_connect_proxy_cb), window);

	gtk_ui_manager_insert_action_group (priv->ui_manager, priv->recent_action_group, -1);
	g_object_unref (priv->recent_action_group);

	priv->recent_merge_id = gtk_ui_manager_new_merge_id (priv->ui_manager);

	recent_items = gtk_recent_manager_get_items (priv->recent_manager);
	recent_items = g_list_sort (recent_items, window_compare_recent_info);

	for (l = recent_items; l && count < MAX_N_RECENT; l = l->next) {
		info = l->data;

		if (gtk_recent_info_has_group (info, PACKAGE)) {
			if (!gtk_recent_info_exists (info))
				continue;

			action_name = g_strdup_printf ("recent-repository-%d", count);
			recent_label = gtk_recent_info_get_uri_display (info);

			action_label = g_string_new (NULL);
			g_string_printf (action_label, "_%d. ", ++count);

			for (s = recent_label; *s; ++s) {
				if ('_' == *s)
					g_string_append_c (action_label, '_');

				g_string_append_c (action_label, *s);
			}

			action = gtk_action_new (action_name, action_label->str, NULL, NULL);
			gtk_action_group_add_action (priv->recent_action_group, action);

			g_object_set_data_full (G_OBJECT (action), "recent-action-path",
						gtk_recent_info_get_uri_display (info),
						(GDestroyNotify) g_free);

			g_signal_connect (action,
					  "activate",
					  G_CALLBACK (window_recent_repository_activate),
					  window);

			gtk_ui_manager_add_ui (priv->ui_manager,
					       priv->recent_merge_id,
					       RECENT_REPOS_PLACEHOLDER_PATH,
					       action_name,
					       action_name,
					       GTK_UI_MANAGER_MENUITEM,
					       FALSE);

			g_string_free (action_label, TRUE);
			g_object_unref (action);

			g_free (recent_label);
			g_free (action_name);
		}
	}

	g_list_foreach (recent_items, (GFunc) gtk_recent_info_unref, NULL);
	g_list_free (recent_items);
}

static void
window_create_recent_manager (GiggleWindow *window)
{
	GiggleWindowPriv *priv;

	priv = window->priv;

	priv->recent_action_group = gtk_action_group_new ("RecentRepositories");
	priv->recent_manager = gtk_recent_manager_get_default ();

	gtk_ui_manager_insert_action_group (priv->ui_manager, priv->recent_action_group, 0);

	g_signal_connect_swapped (priv->recent_manager, "changed",
				  G_CALLBACK (window_recent_repositories_update),
				  window);

	window_recent_repositories_update (window);
}

static void
window_cancel_find (GtkWidget    *widget,
		    GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	GiggleView       *view;

	priv = window->priv;

	view = giggle_view_shell_get_selected (GIGGLE_VIEW_SHELL (priv->view_shell));
	g_return_if_fail (GIGGLE_IS_SEARCHABLE (view));

	giggle_searchable_cancel (GIGGLE_SEARCHABLE (view));

	gtk_widget_hide (widget);
}

static void
window_create_find_bar (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	GtkToolItem      *separator;

	priv = window->priv;

	priv->find_bar = egg_find_bar_new ();

	separator = gtk_separator_tool_item_new ();
	gtk_widget_show (GTK_WIDGET (separator));
	gtk_toolbar_insert (GTK_TOOLBAR (priv->find_bar), separator, -1);

	priv->full_search = gtk_toggle_tool_button_new ();
	gtk_tool_button_set_label (GTK_TOOL_BUTTON (priv->full_search), _("Search Inside _Patches"));
	gtk_tool_button_set_use_underline (GTK_TOOL_BUTTON (priv->full_search), TRUE);
	gtk_tool_item_set_is_important (priv->full_search, TRUE);
	gtk_widget_show (GTK_WIDGET (priv->full_search));

	gtk_toolbar_insert (GTK_TOOLBAR (priv->find_bar), priv->full_search, -1);

	gtk_box_pack_end (GTK_BOX (priv->content_vbox), priv->find_bar, FALSE, FALSE, 0);

	g_signal_connect (priv->find_bar, "close",
			  G_CALLBACK (window_cancel_find), window);
	g_signal_connect (priv->find_bar, "next",
			  G_CALLBACK (window_find_next), window);
	g_signal_connect (priv->find_bar, "previous",
			  G_CALLBACK (window_find_previous), window);
}

static void
window_update_search_ui (GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	gboolean          searchable = FALSE;
	GtkActionGroup   *action_group;
	GiggleView       *view;

	priv = window->priv;

	if (gtk_widget_get_visible (priv->view_shell)) {
		view = giggle_view_shell_get_selected (GIGGLE_VIEW_SHELL (priv->view_shell));
		searchable = GIGGLE_IS_SEARCHABLE (view);
	}

	/* Update find */
	action_group = giggle_ui_manager_get_action_group (priv->ui_manager, "FindActions");
	gtk_action_group_set_sensitive (action_group, searchable);

	if (!searchable)
		gtk_widget_hide (priv->find_bar);
}

static void
window_view_shell_switch_page_cb (GtkNotebook     *notebook,
				  GtkNotebookPage *page,
				  guint            page_num,
				  GiggleWindow    *window)
{
	window_update_search_ui (window);
	window_history_capture (window);
}

static void
window_history_changed_cb (GiggleHistory *history,
			   GiggleWindow  *window)
{
	GiggleWindowPriv *priv;
	GiggleView       *view;

	priv = window->priv;

	view = giggle_view_shell_get_selected (GIGGLE_VIEW_SHELL (priv->view_shell));

	if (GIGGLE_IS_HISTORY (view) && GIGGLE_HISTORY (view) == history)
		window_history_capture (window);
}

static void
window_view_shell_page_added_cb (GtkNotebook  *notebook,
				 GtkWidget    *page,
				 guint         page_num,
				 GiggleWindow *window)
{
	if (GIGGLE_IS_HISTORY (page)) {
		g_signal_connect
			(page, "history-changed",
			  G_CALLBACK (window_history_changed_cb), window);

		g_signal_connect_swapped
			(page, "history-reset",
			 G_CALLBACK (window_history_reset), window);
	}
}

static void
window_view_shell_page_removed_cb (GtkNotebook  *notebook,
				   GtkWidget    *page,
				   guint         page_num,
				   GiggleWindow *window)
{
	window_history_remove_view (window, GIGGLE_VIEW (page));

	g_signal_handlers_disconnect_by_func (page, window_update_search_ui, window);
	g_signal_handlers_disconnect_by_func (page, window_history_changed_cb, window);
}

static void
window_directory_changed_cb (GiggleGit    *git,
			     GParamSpec   *arg,
			     GiggleWindow *window)
{
	GiggleWindowPriv *priv;
	gchar            *title;
	const gchar      *directory;
	GtkActionGroup   *action_group;

	priv = window->priv;

	directory = giggle_git_get_directory (git);
	title = g_strdup_printf ("%s - %s", directory, g_get_application_name ());
	gtk_window_set_title (GTK_WINDOW (window), title);
	g_free (title);

	action_group = giggle_ui_manager_get_action_group (priv->ui_manager, "ProjectActions");
	gtk_action_group_set_sensitive (action_group, TRUE);

	giggle_git_config_update (priv->configuration,
				  configuration_updated_cb, window);
}

static void
window_recent_repositories_add (GiggleWindow *window)
{
	static const gchar *groups[] = { PACKAGE, NULL };
	GiggleWindowPriv *priv;
	GtkRecentData     data = { 0, };
	const gchar      *repository;
	gchar            *tmp_string;

	priv = window->priv;

	repository = giggle_git_get_project_dir (priv->git);

	if (!repository) {
		repository = giggle_git_get_git_dir (priv->git);
	}

	g_return_if_fail (repository != NULL);

	data.display_name = (gchar *) giggle_git_get_project_name (priv->git);
	data.groups = (gchar **) groups;
	data.mime_type = g_strdup ("x-directory/normal");
	data.app_name = (gchar *) g_get_application_name ();
	data.app_exec = g_strjoin (" ", g_get_prgname (), "%u", NULL);

	tmp_string = g_filename_to_uri (repository, NULL, NULL);
	gtk_recent_manager_add_full (priv->recent_manager,
                                     tmp_string, &data);
	g_free (tmp_string);
	g_free (data.mime_type);
	g_free (data.app_exec);
}

static void
window_plugin_added_cb (GigglePluginManager *manager,
			GigglePlugin        *plugin,
			GiggleWindow        *window)
{
	GiggleWindowPriv *priv;
	GError           *error = NULL;

	priv = window->priv;

	/*
	g_print ("%s: %s - %s\n", G_STRFUNC,
		 giggle_plugin_get_filename (plugin),
		 giggle_plugin_get_description (plugin));
	*/

	if (!giggle_plugin_merge_ui (plugin, priv->ui_manager, &error)) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_clear_error (&error);
	}

	gtk_ui_manager_ensure_update (priv->ui_manager);
}

static void
about_activate_link (GtkAboutDialog *about,
                     const char     *uri,
                     gpointer        data)
{
	window_visit_uri (data, uri);
}

static void
button_activate_link (GtkLinkButton *button,
                      const char    *uri,
                      gpointer       data)
{
	window_visit_uri (data, uri);
}

static void
giggle_window_init (GiggleWindow *window)
{
	GiggleWindowPriv *priv;

	window->priv = G_TYPE_INSTANCE_GET_PRIVATE (window,
	                                            GIGGLE_TYPE_WINDOW,
	                                            GiggleWindowPriv);
	priv = window->priv;

	priv->git = giggle_git_get ();
	priv->configuration = giggle_git_config_new ();

	priv->content_vbox = gtk_vbox_new (FALSE, 0);
	window_create_ui_manager (window);

	priv->view_shell = giggle_view_shell_new_with_ui (priv->ui_manager,
							  "WindowViewShellActions");

	priv->history_view = giggle_view_history_new (priv->ui_manager);
	priv->file_view = giggle_view_file_new ();

	window_create_recent_manager (window);
	window_create_find_bar (window);

	giggle_view_shell_add_placeholder (GIGGLE_VIEW_SHELL (priv->view_shell),
				           "/MainToolbar/ViewShell");
	giggle_view_shell_add_placeholder (GIGGLE_VIEW_SHELL (priv->view_shell),
				           "/MainMenubar/ViewMenu/ViewShell");

	g_signal_connect_after (priv->view_shell, "switch-page",
				G_CALLBACK (window_view_shell_switch_page_cb),
				window);
	g_signal_connect_after (priv->view_shell, "page-added",
				G_CALLBACK (window_view_shell_page_added_cb),
				window);
	g_signal_connect_after (priv->view_shell, "page-removed",
				G_CALLBACK (window_view_shell_page_removed_cb),
				window);

	giggle_view_shell_append_view (GIGGLE_VIEW_SHELL (priv->view_shell),
				       GIGGLE_VIEW (priv->file_view));
	giggle_view_shell_append_view (GIGGLE_VIEW_SHELL (priv->view_shell),
				       GIGGLE_VIEW (priv->history_view));

	gtk_box_pack_start (GTK_BOX (priv->content_vbox),
	                    priv->view_shell, TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (window), priv->content_vbox);
	gtk_widget_show_all (priv->content_vbox);
	gtk_widget_hide (priv->view_shell);

	g_signal_connect_after (priv->git, "notify::directory",
				G_CALLBACK (window_directory_changed_cb), window);
	g_signal_connect_swapped (priv->git, "notify::project-dir",
				  G_CALLBACK (window_recent_repositories_add), window);

#if 0
	window_create_menu (window);
#endif

	priv->plugin_manager = giggle_plugin_manager_new ();

	giggle_plugin_manager_add_widget (priv->plugin_manager,
					  "ViewShell", priv->view_shell);

	g_signal_connect (priv->plugin_manager, "plugin-added",
			  G_CALLBACK (window_plugin_added_cb), window);

	gtk_about_dialog_set_url_hook (about_activate_link, window, NULL);
	gtk_link_button_set_uri_hook (button_activate_link, window, NULL);

	window_update_search_ui (window);
	window_history_update_ui (window);
}

#if 0
/* Update revision info. If previous_revision is not NULL, a diff between it and
 * the current revision will be shown.
 */

static void
window_action_save_patch_cb (GtkAction    *action,
			     GiggleWindow *window)
{
/* FIXME: implement this again with GiggleView */
#if 0
	GiggleWindowPriv *priv;
	GtkWidget        *file_chooser;
	GtkTextBuffer    *text_buffer;
	GtkTextIter       iter_start, iter_end;
	gchar            *text, *path;
	GError           *error = NULL;

	priv = window->priv;

	file_chooser = gtk_file_chooser_dialog_new (
		_("Save patch file"),
		GTK_WINDOW (window),
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL);

	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (file_chooser), TRUE);

	/* FIXME: remember the last selected folder */

	if (gtk_dialog_run (GTK_DIALOG (file_chooser)) == GTK_RESPONSE_OK) {
		text_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->diff_textview));
		gtk_text_buffer_get_bounds (text_buffer, &iter_start, &iter_end);

		text = gtk_text_buffer_get_text (text_buffer, &iter_start, &iter_end, TRUE);
		path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_chooser));

		if (!g_file_set_contents (path, text, strlen (text), &error)) {
			window_show_error (window,
					   N_("An error occurred when saving to file:\n%s"),
					   error);
			g_error_free (error);
		}
	}

	gtk_widget_destroy (file_chooser);
#endif
}

static void
window_action_diff_cb (GtkAction    *action,
		       GiggleWindow *window)
{
	giggle_window_show_diff_window (window);
}
#endif

GtkWidget *
giggle_window_new (void)
{
	GtkWidget *window;

	window = g_object_new (GIGGLE_TYPE_WINDOW, NULL);

	return window;
}

GiggleGit *
giggle_window_get_git (GiggleWindow *self)
{
	g_return_val_if_fail (GIGGLE_IS_WINDOW (self), NULL);

	return self->priv->git;
}

void
giggle_window_show_diff_window (GiggleWindow *self)
{
#if 0
	GiggleWindowPriv *priv;

	priv = GET_PRIV (self);

	if (!priv->diff_current_window) {
		priv->diff_current_window = giggle_diff_window_new ();

		gtk_window_set_transient_for (GTK_WINDOW (priv->diff_current_window),
					      GTK_WINDOW (self));
		g_signal_connect (priv->diff_current_window, "delete-event",
				  G_CALLBACK (gtk_widget_hide_on_delete), NULL);
		g_signal_connect_after (priv->diff_current_window, "response",
					G_CALLBACK (gtk_widget_hide), NULL);
	}

	if (GTK_WIDGET_REALIZED (self)) {
		gtk_widget_show (priv->diff_current_window);
	}
#endif
}
