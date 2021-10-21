/* file-storage.c
 *
 * Copyright 2021 cf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "file-storage.h"
#include <openssl/ssl.h>
#include <openssl/aes.h>
#include <json-glib/json-glib.h>

struct _FileStorage {
	GtkFrame parent_instance;

	GdkDisplay *display;
	GtkCssProvider *provider;

	GtkWidget *vbox;
	GtkWidget *box;
	GtkWidget *button_download;
	GtkWidget *label_filename;
	GtkWidget *progress;
	char *filename;
	char *data;
	char *private_key;
	char *ckey;
	char *ivec;
	char *from;
	double fraction;
	GOutputStream *ogio;
	int index;
};

G_DEFINE_TYPE (FileStorage, file_storage, GTK_TYPE_FRAME)

typedef enum {
	PROP_FILENAME = 1,
	PROP_DATA,
	PROP_PRIVATE_KEY,
	PROP_CKEY,
	PROP_IVEC,
	PROP_FROM,
	PROP_OGIO,
	PROP_PROGRESS,
	PROP_INDEX,
	N_PROPERTIES
} FileStorageProperty;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static const char *styles =
"label#label_filename { margin: 16px; }"
"box#storage { margin: 8px; }"
;

static void file_storage_set_property (GObject *object,
		guint property_id,
		const GValue *value,
		GParamSpec *pspec)
{
	FileStorage *self = FILE_STORAGE (object);

	switch ((FileStorageProperty) property_id)
	{
		case PROP_FILENAME:
			if (self->filename) g_free (self->filename);
			self->filename = g_value_dup_string (value);
			gtk_label_set_text (GTK_LABEL (self->label_filename), self->filename);
			break;
		case PROP_DATA:
			if (self->data) g_free (self->data);
			self->data = g_value_dup_string (value);
			break;
		case PROP_PRIVATE_KEY:
			if (self->private_key) g_free (self->private_key);
			self->private_key = g_value_dup_string (value);
			break;
		case PROP_CKEY:
			if (self->ckey) g_free (self->ckey);
			self->ckey = g_value_dup_string (value);
			break;
		case PROP_IVEC:
			if (self->ivec) g_free (self->ivec);
			self->ivec = g_value_dup_string (value);
			break;
		case PROP_FROM:
			if (self->from) g_free (self->from);
			self->from = g_value_dup_string (value);
			break;
		case PROP_OGIO:
			self->ogio = g_value_get_object (value);
			break;
		case PROP_PROGRESS:
			self->fraction = g_value_get_double (value);
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), self->fraction);
			//gtk_progress_bar_pulse (GTK_PROGRESS_BAR (self->progress));
			break;
		case PROP_INDEX:
			self->index = g_value_get_int (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void file_storage_get_property (GObject *object,
		guint property_id,
		GValue *value,
		GParamSpec *pspec)
{
	FileStorage *self = FILE_STORAGE (object);

	switch ((FileStorageProperty) property_id)
	{
		case PROP_FILENAME:
			g_value_set_string (value, self->filename);
			break;
		case PROP_DATA:
			g_value_set_string (value, self->data);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void file_storage_class_init (FileStorageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = file_storage_set_property;
	object_class->get_property = file_storage_get_property;

	obj_properties[PROP_FILENAME] = g_param_spec_string (
			"filename",
			"filename",
			"set filename",
			NULL,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_DATA] = g_param_spec_string (
			"data",
			"data",
			"set data",
			NULL,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_PRIVATE_KEY] = g_param_spec_string (
			"key",
			"key",
			"set key",
			NULL,
			G_PARAM_WRITABLE
			);

	obj_properties[PROP_CKEY] = g_param_spec_string (
			"ckey",
			"ckey",
			"set ckey",
			NULL,
			G_PARAM_WRITABLE
			);

	obj_properties[PROP_IVEC] = g_param_spec_string (
			"ivec",
			"ivec",
			"set ivec",
			NULL,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_FROM] = g_param_spec_string (
			"from",
			"from",
			"from",
			NULL,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_OGIO] = g_param_spec_object (
			"ogio",
			"ogio",
			"ogio",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_PROGRESS] = g_param_spec_double (
			"progress",
			"progress",
			"progress",
			0.0,
			1.0,
			0.0,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_INDEX] = g_param_spec_int (
			"index",
			"index",
			"index",
			0,
			16000,
			0,
			G_PARAM_WRITABLE
			);

	g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}	

static void button_download_clicked_cb (GtkButton *button, gpointer user_data)
{
	FileStorage *self = FILE_STORAGE (user_data);

	JsonBuilder *builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "get_file");
	json_builder_set_member_name (builder, "from");
	json_builder_add_string_value (builder, self->from);
	json_builder_set_member_name (builder, "filename");
	json_builder_add_string_value (builder, self->filename);
	json_builder_set_member_name (builder, "index");
	json_builder_add_int_value (builder, self->index);
	json_builder_end_object (builder);

	JsonNode *node = json_builder_get_root (builder);
	JsonGenerator *gen = json_generator_new ();
	json_generator_set_root (gen, node);
	gsize length = 0;
	char *data = json_generator_to_data (gen, &length);

	g_output_stream_write (G_OUTPUT_STREAM (self->ogio),
			data,
			length,
			NULL,
			NULL
			);

	g_object_unref (builder);
	g_object_unref (gen);
	g_free (data);

}

static void file_storage_init (FileStorage *self)
{
	self->display = gdk_display_get_default ();
	self->provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (self->provider, styles, -1);
	gtk_style_context_add_provider_for_display (self->display, (GtkStyleProvider *) self->provider, GTK_STYLE_PROVIDER_PRIORITY_USER);

	self->label_filename = gtk_label_new ("");
	gtk_widget_set_name (self->label_filename, "label_filename");

	self->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append (GTK_BOX (self->vbox), self->box);
	self->button_download = gtk_button_new ();
	GtkWidget *image_download = gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/download.svg");
	gtk_button_set_child (GTK_BUTTON (self->button_download), image_download);
	gtk_box_append (GTK_BOX (self->box), self->label_filename);
	gtk_box_append (GTK_BOX (self->box), self->button_download);
	g_object_set (self->button_download,
			"halign", GTK_ALIGN_END,
			NULL);

	gtk_frame_set_child (GTK_FRAME (self), self->vbox);
	gtk_widget_set_name (self->box, "storage");
	g_signal_connect (self->button_download, "clicked", G_CALLBACK (button_download_clicked_cb), self);

	self->progress = gtk_progress_bar_new ();
	gtk_box_append (GTK_BOX (self->vbox), self->progress);
}
