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

struct _FileStorage {
	GtkFrame parent_instance;

	GdkDisplay *display;
	GtkCssProvider *provider;

	GtkWidget *box;
	GtkWidget *button_download;
	GtkWidget *label_filename;
	char *filename;
	char *data;
	char *private_key;
};

G_DEFINE_TYPE (FileStorage, file_storage, GTK_TYPE_FRAME)

typedef enum {
	PROP_FILENAME = 1,
	PROP_DATA,
	PROP_PRIVATE_KEY,
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

	g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}	

static unsigned char get_hex (char d, int cq)
{
        switch (d) {
                case '0'...'9':
                        return (d - 48) * cq;
                case 'a'...'f':
                        return (d - 97 + 10) * cq;
                default:
                        return 0;
        }
}

static unsigned char *convert_data_to_hex (const char *data, size_t *ll)
{
        *ll = 0;
        int len = strlen (data);
        unsigned char *dt = calloc (len, 1);

        unsigned char *d = dt;
        for (int i = 0; i < len; i += 2) {
                *d = get_hex (data[i + 0], 16) + get_hex (data[i + 1], 1);
                d++;
                (*ll)++;
        }

        return dt;
}


static void button_download_clicked_cb (GtkButton *button, gpointer user_data)
{
	FileStorage *self = FILE_STORAGE (user_data);
	 
        unsigned char *to = calloc (1024 * 1024 * 40, 1);
        if (!to) return;

        FILE *fp = fopen (self->private_key, "rb");
        if (!fp) {
                free (to);
                return;
        }

        size_t len;
        unsigned char *buffer = convert_data_to_hex (self->data, &len);

        int padding = RSA_PKCS1_PADDING;

        RSA *rsa = PEM_read_RSAPrivateKey (fp, NULL, NULL, NULL);

        long int encrypted_length = RSA_private_decrypt (len, buffer, to, rsa, padding);
	g_print ("download enc: %ld\n", encrypted_length);
        RSA_free (rsa);

	FILE *afp = fopen ("/home/cf/test.pdf", "w");
	fprintf (afp, "%s", to);
	fclose (afp);
        free (to);
        fclose (fp);
}

static void file_storage_init (FileStorage *self)
{
	self->display = gdk_display_get_default ();
	self->provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (self->provider, styles, -1);
	gtk_style_context_add_provider_for_display (self->display, (GtkStyleProvider *) self->provider, GTK_STYLE_PROVIDER_PRIORITY_USER);

	self->label_filename = gtk_label_new ("");
	gtk_widget_set_name (self->label_filename, "label_filename");

	self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	self->button_download = gtk_button_new ();
	GtkWidget *image_download = gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/download.svg");
	gtk_button_set_child (GTK_BUTTON (self->button_download), image_download);
	gtk_box_append (GTK_BOX (self->box), self->label_filename);
	gtk_box_append (GTK_BOX (self->box), self->button_download);
	g_object_set (self->button_download,
			"halign", GTK_ALIGN_END,
			NULL);

	gtk_frame_set_child (GTK_FRAME (self), self->box);
	gtk_widget_set_name (self->box, "storage");
	g_signal_connect (self->button_download, "clicked", G_CALLBACK (button_download_clicked_cb), self);
}
