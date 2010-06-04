#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GIGGLE_TYPE_CLONE_DIALOG         (giggle_clone_dialog_get_type ())
#define GIGGLE_CLONE_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GIGGLE_TYPE_CLONE_DIALOG, GiggleCloneDialog))
#define GIGGLE_CLONE_DIALOG_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), GIGGLE_TYPE_CLONE_DIALOG, GiggleCloneDialogClass))
#define GIGGLE_IS_CLONE_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GIGGLE_TYPE_CLONE_DIALOG))
#define GIGGLE_IS_CLONE_DIALOG_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), GIGGLE_TYPE_CLONE_DIALOG))
#define GIGGLE_CLONE_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GIGGLE_TYPE_CLONE_DIALOG, GiggleCloneDialogClass))

typedef struct _GiggleCloneDialog        GiggleCloneDialog;
typedef struct _GiggleCloneDialogClass   GiggleCloneDialogClass;
typedef struct _GiggleCloneDialogPrivate GiggleCloneDialogPrivate;

struct _GiggleCloneDialog {
	GtkDialog parent;

	GiggleCloneDialogPrivate *priv;
};

struct _GiggleCloneDialogClass {
	GtkDialogClass parent_class;
};

GType        giggle_clone_dialog_get_type      (void) G_GNUC_CONST;

GtkWidget   *giggle_clone_dialog_new           (const gchar *repo,
                                                const gchar *dir);
const gchar *giggle_clone_dialog_get_directory (GiggleCloneDialog *dialog);

G_END_DECLS
