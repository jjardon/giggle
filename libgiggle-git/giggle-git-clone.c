/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2010 Florian Müllner
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
#include "giggle-git-clone.h"

struct GiggleGitClonePrivate {
	gchar *repo;
	gchar *directory;
};

static void     git_clone_finalize            (GObject         *object);
static void     git_clone_get_property        (GObject         *object,
                                               guint            param_id,
                                               GValue          *value,
                                               GParamSpec      *pspec);
static void     git_clone_set_property        (GObject         *object,
                                               guint            param_id,
                                               const GValue    *value,
                                               GParamSpec      *pspec);

static gboolean git_clone_get_command_line    (GiggleJob       *job,
                                               gchar          **command_line);


G_DEFINE_TYPE (GiggleGitClone, giggle_git_clone, GIGGLE_TYPE_JOB)


enum {
	PROP_0,
	PROP_REPO,
	PROP_DIR
};

static void
giggle_git_clone_class_init (GiggleGitCloneClass *class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (class);
	GiggleJobClass *job_class    = GIGGLE_JOB_CLASS (class);

	object_class->finalize     = git_clone_finalize;
	object_class->get_property = git_clone_get_property;
	object_class->set_property = git_clone_set_property;

	job_class->get_command_line = git_clone_get_command_line;

	g_object_class_install_property (object_class,
	                                 PROP_REPO,
	                                 g_param_spec_string ("repo",
	                                                      "Repo",
	                                                      "Cloned repository",
	                                                      NULL,
	                                                      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
	                                 PROP_DIR,
	                                 g_param_spec_string ("directory",
	                                                      "Directory",
	                                                      "Directory for clone",
	                                                      NULL,
	                                                      G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GiggleGitClonePrivate));
}

static void
giggle_git_clone_init (GiggleGitClone *clone)
{
	clone->priv = G_TYPE_INSTANCE_GET_PRIVATE (clone,
						   GIGGLE_TYPE_GIT_CLONE,
						   GiggleGitClonePrivate);
}

static void
git_clone_finalize (GObject *object)
{
	GiggleGitClonePrivate *priv;

	priv = GIGGLE_GIT_CLONE (object)->priv;

	g_free (priv->repo);

	G_OBJECT_CLASS (giggle_git_clone_parent_class)->finalize (object);
}

static void
git_clone_get_property (GObject    *object,
                        guint       param_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	GiggleGitClonePrivate *priv;

	priv = GIGGLE_GIT_CLONE (object)->priv;

	switch (param_id) {
	case PROP_REPO:
		g_value_set_string (value, priv->repo);
		break;
	case PROP_DIR:
		g_value_set_string (value, priv->directory);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
git_clone_set_property (GObject      *object,
                        guint         param_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	GiggleGitClonePrivate *priv;

	priv = GIGGLE_GIT_CLONE (object)->priv;

	switch (param_id) {
	case PROP_REPO:
		priv->repo = g_value_dup_string (value);
		break;
	case PROP_DIR:
		priv->directory = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static gboolean
git_clone_get_command_line (GiggleJob *job, gchar **command_line)
{
	GiggleGitClonePrivate *priv;
	GString             *str;

	priv = GIGGLE_GIT_CLONE (job)->priv;

	g_return_val_if_fail (priv->repo != NULL, FALSE);
	str = g_string_new (GIT_COMMAND " clone  ");

	g_string_append_printf (str, "\"%s\" \"%s\"",
	                        priv->repo, priv->directory);

	*command_line = g_string_free (str, FALSE);
	return TRUE;
}

GiggleJob *
giggle_git_clone_new (const gchar *repo, const gchar *directory)
{
	return g_object_new (GIGGLE_TYPE_GIT_CLONE,
			     "repo", repo,
	                     "directory", directory,
			     NULL);
}

const gchar *
giggle_git_clone_get_directory (GiggleGitClone *clone)
{
	g_return_val_if_fail (GIGGLE_IS_GIT_CLONE (clone), NULL);

	return clone->priv->directory;
}

const gchar *
giggle_git_clone_get_repo (GiggleGitClone *clone)
{
	g_return_val_if_fail (GIGGLE_IS_GIT_CLONE (clone), NULL);

	return clone->priv->repo;
}
