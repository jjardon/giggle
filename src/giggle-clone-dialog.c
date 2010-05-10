#include <glib/gi18n.h>

#include <libgiggle-git/giggle-git.h>
#include <libgiggle-git/giggle-git-clone.h>
#include <giggle-clone-dialog.h>
#include <giggle-helpers.h>

G_DEFINE_TYPE (GiggleCloneDialog, giggle_clone_dialog, GTK_TYPE_DIALOG);

#define GIGGLE_CLONE_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIGGLE_TYPE_CLONE_DIALOG, GiggleCloneDialogPrivate))

struct _GiggleCloneDialogPrivate {
	GiggleGit *git;

	GtkWidget *remote_entry;
	GtkWidget *local_entry;
	GtkWidget *folder_button;
	GtkWidget *progress_bar;
	GtkWidget *clone_button;

	guint pulse_timeout_id;

	gchar *git_directory;
};

static gboolean
entry_has_input (GtkWidget *entry)
{
	const char *text;

	if (entry == NULL)
		return FALSE;

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	return text != NULL && *text != '\0';
}

static gboolean
input_is_valid (GiggleCloneDialog *dialog)
{
	gboolean is_valid = FALSE;

	is_valid = entry_has_input (dialog->priv->remote_entry) &&
	           entry_has_input (dialog->priv->local_entry);
	return is_valid;
}

static void
verify_input (GiggleCloneDialog *dialog, GtkEditable *editable)
{
	gtk_widget_set_sensitive (dialog->priv->clone_button,
	                          input_is_valid (dialog));
}

static void
cycle_focus (GiggleCloneDialog *dialog, GtkWidget *widget)
{
	GtkWidget *next_widget = NULL;

	if (input_is_valid (dialog))
		next_widget = dialog->priv->clone_button;
	else if (widget == dialog->priv->remote_entry)
		next_widget = dialog->priv->local_entry;

	if (next_widget)
		gtk_widget_grab_focus (next_widget);
}

static gboolean
update_progress_bar (gpointer data)
{
	GiggleCloneDialog *dialog;

	dialog = GIGGLE_CLONE_DIALOG (data);
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (dialog->priv->progress_bar));

	return TRUE;
}

static gchar *
create_local_name_from_remote (const gchar *repo)
{
	gchar *start, *rv;
	const gchar *end;

	start = g_path_get_basename (repo);
	if (g_strcmp0 (start, ".") == 0)
		end = start;
	else if (g_str_has_suffix (start, ".git"))
		end = strstr (start, ".git");
	else
		end = start + strlen (start);

	rv = g_strndup (start, end - start);

	g_free (start);

	return rv;
}

static gboolean
preset_local_name (GiggleCloneDialog *dialog,
                   GdkEventFocus     *event,
                   GtkWidget         *widget)
{
	if (!entry_has_input (widget))
		return FALSE;

	if (!entry_has_input (dialog->priv->local_entry)) {
		const gchar *remote;
		gchar *local;

		remote = gtk_entry_get_text (GTK_ENTRY (dialog->priv->remote_entry));
		local  = create_local_name_from_remote (remote);
		gtk_entry_set_text (GTK_ENTRY (dialog->priv->local_entry),
		                    local);
		g_free (local);
	}

	return FALSE;
}

static void
clone_repository_finished (GiggleGit *git,
                           GiggleJob *job,
                           GError    *error,
                           gpointer   data)
{
	GiggleCloneDialog *dialog = GIGGLE_CLONE_DIALOG (data);

	g_source_remove (dialog->priv->pulse_timeout_id);

	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (dialog->priv->progress_bar), 1.0);
	gtk_widget_hide (dialog->priv->clone_button);

	gtk_dialog_add_button (GTK_DIALOG (dialog),
	                       GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
	                                 GTK_RESPONSE_ACCEPT);
}

static void
warning_dialog (GtkWindow *parent)
{
	GtkWidget *widget;

	widget = gtk_message_dialog_new (parent,
	                                 GTK_DIALOG_DESTROY_WITH_PARENT,
	                                 GTK_MESSAGE_ERROR,
	                                 GTK_BUTTONS_CLOSE,
	                                 "You must fill out both fields of dialogue");
	gtk_dialog_run (GTK_DIALOG (widget));
	gtk_widget_destroy (widget);
}

static void
clone_repository (GiggleCloneDialog *dialog, GtkButton *button)
{
	GiggleJob *job;
	GtkFileChooser *chooser;
	const gchar  *repo, *name;
	gchar *base;

	if (input_is_valid (dialog) == FALSE) {
		warning_dialog (GTK_WINDOW (dialog));
		return;
	}

	gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
	gtk_widget_set_sensitive (dialog->priv->remote_entry, FALSE);
	gtk_widget_set_sensitive (dialog->priv->local_entry, FALSE);
	gtk_widget_set_sensitive (dialog->priv->folder_button, FALSE);

	dialog->priv->pulse_timeout_id = g_timeout_add (100,
	                                                update_progress_bar,
	                                                dialog);

	chooser = GTK_FILE_CHOOSER (dialog->priv->folder_button);
	base = gtk_file_chooser_get_filename (chooser);

	name = gtk_entry_get_text (GTK_ENTRY (dialog->priv->local_entry));
	repo = gtk_entry_get_text (GTK_ENTRY (dialog->priv->remote_entry));

	dialog->priv->git_directory = g_build_filename (base, name, NULL);
	g_free (base);

	job = giggle_git_clone_new (repo, dialog->priv->git_directory);

	giggle_git_run_job (dialog->priv->git, job,
	                    clone_repository_finished, dialog);
}

static void
set_table_row (GtkTable *table,
               GtkWidget *label, GtkWidget *widget,
               gpointer user_data)
{
	static guint row = 0;
	guint col = 0;

	g_return_if_fail (GTK_IS_TABLE (table));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	if (label) {
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
		gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
		gtk_widget_show (label);

		gtk_table_attach (GTK_TABLE (table), label,
	                          0, ++col, row, row + 1,
		                  GTK_FILL, 0, 0, 0);
	}

	if (GTK_IS_EDITABLE (widget)) {
		g_signal_connect_swapped (widget, "changed",
		                          G_CALLBACK (verify_input), user_data);
		g_signal_connect_swapped (widget, "activate",
		                          G_CALLBACK (cycle_focus), user_data);
	}

	gtk_widget_show (widget);

	gtk_table_attach (GTK_TABLE (table), widget,
	                  col, 2, row, row + 1,
	                  GTK_EXPAND | GTK_FILL, 0, 0, 0);
	row++;
}

GtkWidget *
giggle_clone_dialog_new (const gchar *repo, const gchar *dir) {
	GiggleCloneDialogPrivate *priv;
	GiggleCloneDialog *dialog;
	GtkWidget *table, *box;
	GtkWidget *action_box;

	dialog = g_object_new (GIGGLE_TYPE_CLONE_DIALOG,
	                       "title", _("Clone Repository"),
	                       "has-separator", FALSE,
	                       "border-width", 12,
	                       NULL);
	priv = dialog->priv;

	/* set up action area */
	gtk_dialog_add_button (GTK_DIALOG (dialog),
	                       GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	priv->clone_button = gtk_button_new_with_mnemonic (_("Cl_one"));
	gtk_widget_show (priv->clone_button);

	g_signal_connect_swapped (priv->clone_button, "clicked",
	                          G_CALLBACK (clone_repository), dialog);

	action_box = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
	gtk_box_pack_end (GTK_BOX (action_box), priv->clone_button,
	                  FALSE, FALSE, 0);

	/* setup content area */
	box = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_box_set_spacing (GTK_BOX (box), 18);

	table = gtk_table_new (4, 2, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 12);
	gtk_table_set_row_spacings (GTK_TABLE (table), 6);
	gtk_widget_show (table);

	gtk_box_pack_start (GTK_BOX (box), table, FALSE, FALSE, 0);

	priv->remote_entry = gtk_entry_new ();
	gtk_entry_set_width_chars (GTK_ENTRY (priv->remote_entry), 40);
	set_table_row (GTK_TABLE (table),
	               gtk_label_new_with_mnemonic (_("_Repository:")),
	               priv->remote_entry,
	               dialog);
	g_signal_connect_swapped (priv->remote_entry, "focus-out-event",
	                          G_CALLBACK (preset_local_name), dialog);

	priv->local_entry = gtk_entry_new ();
	set_table_row (GTK_TABLE (table),
	               gtk_label_new_with_mnemonic (_("_Local name:")),
	               priv->local_entry,
	               dialog);

	priv->folder_button = gtk_file_chooser_button_new (
			_("Please select a location for the clone"),
			GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	set_table_row (GTK_TABLE (table),
	               gtk_label_new_with_mnemonic (_("Clone in _folder:")),
	               priv->folder_button,
	               dialog);

	priv->progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (box), priv->progress_bar,
	                    FALSE, FALSE, 0);
	gtk_widget_show (priv->progress_bar);

	if (repo) {
		gchar *repo_name;

		repo_name = create_local_name_from_remote (repo);
		gtk_entry_set_text (GTK_ENTRY (priv->remote_entry),
		                    repo);
		gtk_entry_set_text (GTK_ENTRY (priv->local_entry),
		                    repo_name);
		g_free (repo_name);
	}

	if (dir)
		gtk_file_chooser_set_current_folder (
			GTK_FILE_CHOOSER (priv->folder_button), dir);

	return GTK_WIDGET (dialog);
}

const gchar *
giggle_clone_dialog_get_directory (GiggleCloneDialog *dialog)
{
	g_return_val_if_fail (GIGGLE_IS_CLONE_DIALOG (dialog), NULL);

	return dialog->priv->git_directory;
}

static void
giggle_clone_dialog_dispose (GObject *object)
{
	GiggleCloneDialog *dialog = GIGGLE_CLONE_DIALOG (object);

	if (dialog->priv->git) {
		g_object_unref (dialog->priv->git);
		dialog->priv->git = NULL;
	}

	G_OBJECT_CLASS (giggle_clone_dialog_parent_class)->dispose (object);
}

static void
giggle_clone_dialog_finalize (GObject *object)
{
	GiggleCloneDialog *dialog = GIGGLE_CLONE_DIALOG (object);

	if (dialog->priv->git_directory) {
		g_free (dialog->priv->git_directory);
		dialog->priv->git_directory = NULL;
	}

	if (dialog->priv->pulse_timeout_id) {
		g_source_remove (dialog->priv->pulse_timeout_id);
		dialog->priv->pulse_timeout_id = 0;
	}

	G_OBJECT_CLASS (giggle_clone_dialog_parent_class)->finalize (object);
}

static void
giggle_clone_dialog_init (GiggleCloneDialog *dialog)
{
	dialog->priv = GIGGLE_CLONE_DIALOG_GET_PRIVATE (dialog);
	dialog->priv->git = giggle_git_get ();
	dialog->priv->git_directory = NULL;
}

static void
giggle_clone_dialog_response (GtkDialog *dialog, GtkResponseType response_id)
{
	GiggleCloneDialog *clone;

	clone = GIGGLE_CLONE_DIALOG (dialog);
	if (response_id == GTK_RESPONSE_REJECT
	    && clone->priv->git_directory) {
		giggle_remove_directory_recursive (clone->priv->git_directory);

		g_free (clone->priv->git_directory);
		clone->priv->git_directory = NULL;
	}
}

static void
giggle_clone_dialog_class_init (GiggleCloneDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (klass);

	object_class->dispose = giggle_clone_dialog_dispose;
	object_class->finalize = giggle_clone_dialog_finalize;

	dialog_class->response = giggle_clone_dialog_response;

	g_type_class_add_private (object_class,
	                          sizeof (GiggleCloneDialogPrivate));
}
