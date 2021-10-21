/* main-window.c
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


#include "main-window.h"
#include "user-item.h"
#include "file-storage.h"
#include <json-glib/json-glib.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "cert.h"
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <gst/gst.h>

extern char *root_app;
extern char *root_sounds;
extern char *download_app;

struct _gd {
	char *from;
	char *filename;
	char *eckey;
	char *eivec;
	char *data;
	size_t pos;
	size_t size_buf;
	int filled;
	int index;
	struct _gd *next;
};
struct gd {
	struct _gd *start;
};

struct _MainWindow {
	GtkWindow parent_instance;

	GMutex mutex;

	GtkApplication *app;
	struct gd *gd;
	GThread *get_file;

	GdkDisplay *display;
	GtkCssProvider *provider;
	GSocketConnection *conn;
	GNotification *notification;

	GtkWidget *header_bar;
	GtkWidget *main_pane;
	GtkWidget *frame_list;
	GtkWidget *frame_chat;
	GtkWidget *scroll_list;
	GtkWidget *list_users;
	GtkWidget *left_pane_button;
	GtkWidget *image_header_left_pane;
	GtkWidget *image_handshake;
	GtkWidget *image_header_menu;
	GtkWidget *menu_button;
	GtkWidget *handshake_button;
	GtkWidget *storage_button;
	GtkWidget *image_storage;
	GtkWidget *storage_window;
	GtkWidget *list_box_storage;
	GtkWidget *scroll_list_box;
	GMenu *menu;

	GIOStream *gio;
	GInputStream *igio;
	GOutputStream *ogio;
	char *buf;

	JsonReader *reader;
	GstElement *new_message;

	int index_storage;
	GThread *thread_receive;

};

G_DEFINE_TYPE (MainWindow, main_window, GTK_TYPE_WINDOW)

#define TOTAL_SIZE      16384

GtkWidget *main_window_get_list_box (MainWindow *self)
{
  return self->list_users;
}

static void user_item_row_selected_cb (GtkListBox *box,
		GtkListBoxRow *row,
		gpointer user_data)
{
	MainWindow *self = MAIN_WINDOW (user_data);
  (void) box;
	GtkWidget *child = gtk_list_box_row_get_child (row);
	int handshaking;
	g_object_get (child,
			"handshaking", &handshaking,
			NULL);
	int blink_handshaking;
	g_object_get (child,
			"blink_handshake", &blink_handshaking,
			NULL);

	if (!blink_handshaking) {
		g_object_set (child,
			"blink", 0,
			NULL);
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->handshake_button), handshaking);
	user_item_set_chat (USER_ITEM (child));
}

struct _file_storage {
	char *from;
	char *filename;
	MainWindow *self;
};

static gboolean idle_fill_file_storage (gpointer user_data)
{
	struct _file_storage *fs = (struct _file_storage *) user_data;

	GtkListBoxRow *row_sel_child = gtk_list_box_get_selected_row (GTK_LIST_BOX (fs->self->list_users));
	GtkWidget *sel_child = gtk_list_box_row_get_child (row_sel_child);
	char *name_from = NULL;
	g_object_get (sel_child,
			"name", &name_from,
			NULL);

	if (!strncmp (name_from, fs->from, strlen (fs->from) + 1))
	{
		char path[255];
		snprintf (path, 255, "%s/%s/crypto.pem", root_app, fs->from );
		GtkWidget *item = g_object_new (FILE_TYPE_STORAGE,
				"filename", fs->filename,
				"from", fs->from,
				"key", path,
				"ogio", fs->self->ogio,
				"index", fs->self->index_storage++,
				NULL);
		gtk_list_box_append (GTK_LIST_BOX (fs->self->list_box_storage), item);
	}
	free (fs->from);
	free (fs->filename);
	free (fs);

	return G_SOURCE_REMOVE;
}
static void fill_file_storage (MainWindow *self)
{
	json_reader_read_member (self->reader, "from");
	JsonNode *jfrom = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "filename");
	JsonNode *jname = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	const char *from = json_node_get_string (jfrom);
	const char *filename = json_node_get_string (jname);

	struct _file_storage *fs = calloc (1, sizeof (struct _file_storage));
	fs->from = strdup (from);
	fs->filename = strdup (filename);
	fs->self = self;
	g_idle_add (idle_fill_file_storage, fs);

}

struct _fill_arrays {
	MainWindow *self;
	char *name;
	int status;
	int is_end;
};

static gboolean idle_fill_arrays (gpointer user_data)
{
	struct _fill_arrays *fs = (struct _fill_arrays *) user_data;

	GtkWidget *user_item = g_object_new (USER_TYPE_ITEM, 
			"handbutton", fs->self->handshake_button, 
			"name", fs->name, 
			"status", fs->status, 
			"blink", 0, 
			"frame_chat", fs->self->frame_chat, 
			"app", fs->self->app, 
			"notification", fs->self->notification, 
			"ogio", fs->self->ogio, 
			"main_window", fs->self, 
			NULL);

	gtk_list_box_append (GTK_LIST_BOX (fs->self->list_users), user_item);

	if (fs->is_end)
		gtk_list_box_invalidate_sort (GTK_LIST_BOX (fs->self->list_users));

	free (fs->name);
	free (fs);

	return G_SOURCE_REMOVE;
}

static void fill_arrays (MainWindow *self)
{


	json_reader_is_object (self->reader);

	int count = json_reader_count_members (self->reader);

	json_reader_read_member (self->reader, "users");
	size_t length = json_reader_count_elements (self->reader);

	for (size_t i = 0; i < length; i++) {
		json_reader_read_element (self->reader, i);

		json_reader_read_member (self->reader, "name");
		JsonNode *jname = json_reader_get_value (self->reader);
		json_reader_end_member (self->reader);
		const char *name = json_node_get_string (jname);

		json_reader_read_member (self->reader, "status");
		int status = json_reader_get_int_value (self->reader);
		json_reader_end_member (self->reader);
		struct _fill_arrays *fs = calloc (1, sizeof (struct _fill_arrays));
		fs->self = self;
		fs->name = strdup (name);
		fs->status = status;
		fs->is_end = i + 1 == length ? 1 : 0;
		g_idle_add (idle_fill_arrays, fs);

		json_reader_end_element (self->reader);
	}
	json_reader_end_member (self->reader);
}

struct _fill_arrays_new_status {
	int status;
	char *name;
	MainWindow *self;
};

static gboolean idle_fill_arrays_new_status (gpointer user_data)
{
	struct _fill_arrays_new_status *fs = (struct _fill_arrays_new_status *) user_data;

	for (int i = 0; 1; i++) {
		GtkListBoxRow *row = gtk_list_box_get_row_at_index (
				GTK_LIST_BOX (fs->self->list_users),
				i);
		if (row == NULL) break;

		GtkWidget *item = gtk_list_box_row_get_child (row);
		const char *n = user_item_get_name (USER_ITEM (item));
		if (!strncmp (n, fs->name, strlen (fs->name) + 1)) {
			user_item_set_status (USER_ITEM (item), fs->status);

			gtk_list_box_invalidate_sort (GTK_LIST_BOX (fs->self->list_users));
			goto end;
		}
	}

	GtkWidget *user_item = user_item_new ();

	g_object_set (user_item,
                "name", fs->name,
                "handbutton", fs->self->handshake_button,
                "status", fs->status,
                "blink", 0,
                "frame_chat", fs->self->frame_chat,
                "app", fs->self->app,
                "notification", fs->self->notification,
                "ogio", fs->self->ogio,
                "main_window", fs->self,
                NULL);

	gtk_list_box_append (GTK_LIST_BOX (fs->self->list_users), user_item);
	gtk_list_box_invalidate_sort (GTK_LIST_BOX (fs->self->list_users));

end:
	free (fs->name);
	free (fs);

	return G_SOURCE_REMOVE;
}

static void fill_arrays_new_status (MainWindow *self)
{

	json_reader_read_member (self->reader, "status");
	int status = json_reader_get_int_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "name");
	JsonNode *jname = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	const char *name = json_node_get_string (jname);

	struct _fill_arrays_new_status *fs = calloc (1, sizeof (struct _fill_arrays_new_status));
	fs->status = status;
	fs->name = strdup (name);
	fs->self = self;
	g_idle_add (idle_fill_arrays_new_status, fs);
}

static char *escape_n_from_temp_key (const char *temp_key)
{
	int len = strlen (temp_key);
	char *key = malloc (len + 1);
	int index = 0;
	for (int i = 0; i < len; i++) {
		if (temp_key[i + 0] == '\\' && temp_key[i + 1] == 'n') {
			i++;
			key[index++] = '\n';
			continue;
		}
		key[index++] = temp_key[i];
	}
	key[index] = 0;

	return key;
}

static GtkWidget *get_child_by_name (GtkWidget *, const char *from);

struct _handshake_key_save {
	MainWindow *self;
	char *from;
	char *key;
};

static gboolean idle_handshake_key_save (gpointer user_data)
{
	struct _handshake_key_save *fs = (struct _handshake_key_save *) user_data;

	GtkWidget *child = get_child_by_name (fs->self->list_users, fs->from);
	if (child) {
		g_object_set (child,
			"blink_handshake", 0,
			"handshaking", 0,
			NULL);
		GtkListBoxRow *row_sel_child = gtk_list_box_get_selected_row (GTK_LIST_BOX (fs->self->list_users));
		GtkWidget *sel_child = gtk_list_box_row_get_child (row_sel_child);
		if (child == sel_child) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fs->self->handshake_button), 0);
		}
	}


	char path[256];
	snprintf (path, 256, "%s/%s/crypto.pem", root_app, fs->from);
	remove (path);
	GFile *file = g_file_new_for_path (path);
	if (!file) {
		goto end;
	}

	GFileOutputStream *out = g_file_create (file,
			G_FILE_CREATE_REPLACE_DESTINATION,
			NULL,
			NULL);
	if (!out) {
		goto end;
	}

	g_output_stream_write (G_OUTPUT_STREAM (out),
			fs->key,
			strlen (fs->key),
			NULL,
			NULL
			);

	g_output_stream_close (G_OUTPUT_STREAM (out),
			NULL,
			NULL);
end:
	free (fs->key);
	free (fs->from);
	free (fs);

	return G_SOURCE_REMOVE;
}

static void handshake_key_save (MainWindow *self)
{
	json_reader_read_member (self->reader, "from");
	JsonNode *jfrom = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "key");
	JsonNode *jkey = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	const char *from = json_node_get_string (jfrom);
	const char *temp_key = json_node_get_string (jkey);
	char *key = escape_n_from_temp_key (temp_key);
	struct _handshake_key_save *fs = calloc (1, sizeof (struct _handshake_key_save));
	fs->from = strdup (from);
	fs->key = strdup (key);
	fs->self = self;
	g_idle_add (idle_handshake_key_save, fs);

	free (key);

	return;
}
struct _handshake_answer_status {
	MainWindow *self;
	int status;
	char *name;
};

static gboolean idle_handshake_answer_status (gpointer user_data)
{
	struct _handshake_answer_status *fs = (struct _handshake_answer_status *) user_data;

	for (int i = 0; 1; i++) {
		GtkListBoxRow *row = gtk_list_box_get_row_at_index (
				GTK_LIST_BOX (fs->self->list_users),
				i);
		if (row == NULL) break;

		GtkWidget *item = gtk_list_box_row_get_child (row);
		const char *n = user_item_get_name (USER_ITEM (item));
		if (!strncmp (n, fs->name, strlen (fs->name) + 1)) {
			g_object_set (item,
					"handshaking", fs->status,
					NULL
				     );

			goto end;
		}
	}
end:
	free (fs->name);
	free (fs);

	return G_SOURCE_REMOVE;
}

static void handshake_answer_status (MainWindow *self)
{
	json_reader_read_member (self->reader, "status_handshake");
	JsonNode *jstatus = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);
	json_reader_read_member (self->reader, "to_name");
	JsonNode *jto_name = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	int status = json_node_get_int (jstatus);
	const char *name = json_node_get_string (jto_name);

	struct _handshake_answer_status *fs = calloc (1, sizeof (struct _handshake_answer_status));
	fs->status = status;
	fs->name = strdup (name);
	fs->self = self;
	g_idle_add (idle_handshake_answer_status, fs);

}

#define FROM_TO_ME             0
#define FROM_ME_TO             1

static void read_message_from (GtkWidget *child,
		const char *name,
		char *path,
		const unsigned char *buffer,
		MainWindow *self,
		size_t buffer_len,
		const int show_notification)
{
  (void) self;
	unsigned char *to = calloc (1024 * 1024 * 30, 1);
	if (!to) return;

	FILE *fp = fopen (path, "rb");
	if (!fp) {
		free (to);
		return;
	}

	int padding = RSA_PKCS1_PADDING;

	RSA *rsa = PEM_read_RSAPrivateKey (fp, NULL, NULL, NULL);

	int encrypted_length = RSA_private_decrypt (buffer_len, buffer, to, rsa, padding);
  (void) encrypted_length;
	RSA_free (rsa);

	user_item_add_message (USER_ITEM (child), to, FROM_TO_ME, name, show_notification);

	free (to);
	fclose (fp);
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
static GtkWidget *get_child_by_name (GtkWidget *list_users, const char *from)
{
	for (int i = 0; 1; i++) {
		GtkListBoxRow *row = gtk_list_box_get_row_at_index (
				GTK_LIST_BOX (list_users),
				i);
		if (row == NULL) break;

		GtkWidget *item = gtk_list_box_row_get_child (row);
		const char *n = user_item_get_name (USER_ITEM (item));
		if (!strncmp (n, from, strlen (from) + 1)) {
			return item;
		}
	}
	return NULL;
}

struct _handshake_notice {
	MainWindow *self;
	char *from;
	int status;
};

static gboolean idle_handshake_notice (gpointer user_data)
{
	struct _handshake_notice *fs = (struct _handshake_notice *) user_data;

	GtkWidget *child = get_child_by_name (fs->self->list_users, fs->from);
	if (!child) {
		return;
	}

	g_object_set (child,
			"blink_handshake", fs->status,
			NULL);

	free (fs->from);
	free (fs);

	return G_SOURCE_REMOVE;
}

static void handshake_notice (MainWindow *self)
{

	json_reader_read_member (self->reader, "from");
	JsonNode *jfrom = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);
	json_reader_read_member (self->reader, "status");
	JsonNode *jstatus = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);
	const char *from = json_node_get_string (jfrom);
	const int status = json_node_get_int (jstatus);

	struct _handshake_notice *fs = calloc (1, sizeof (struct _handshake_notice));
	fs->from = strdup (from);
	fs->status = status;
	fs->self = self;
	g_idle_add (idle_handshake_notice, fs);
}

struct _got_message {
	char *from;
	char *data;
	MainWindow *self;
};

static gboolean idle_got_message (gpointer user_data)
{
	struct _got_message *fs = (struct _got_message *) user_data;

	size_t len;
	unsigned char *dt = convert_data_to_hex (fs->data, &len);

	GtkWidget *child = get_child_by_name (fs->self->list_users, fs->from);
	if (!child) {
		goto end;
	}
	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (fs->self->list_users));
	GtkWidget *row_child = NULL;
	if (row)
		row_child = gtk_list_box_row_get_child (row);

	char path[256];
	snprintf (path, 256, "%s/%s/key.pem", root_app, fs->from);

	int show_notification = 0;
	if (child != row_child) {
		show_notification = 1;
	}
	read_message_from (child, fs->from, path, dt, fs->self, len, show_notification);

	if (child != row_child) {
		g_object_set (child,
				"blink", 1,
				NULL);

	}

	main_window_play_new_message (fs->self);

end:
	free (dt);
	free (fs->from);
	free (fs->data);
	free (fs);

	return G_SOURCE_REMOVE;
}

static void got_message (MainWindow *self)
{

	json_reader_read_member (self->reader, "from");
	JsonNode *jfrom = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);
	json_reader_read_member (self->reader, "data");
	JsonNode *jdata = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	const char *from = json_node_get_string (jfrom);
	const char *data = json_node_get_string (jdata);
	struct _got_message *fs = calloc (1, sizeof (struct _got_message));
	fs->from = strdup (from);
	fs->data = strdup (data);
	fs->self = self;
	g_idle_add (idle_got_message, fs);
}

static char *_rsa_decrypt (const char *private_key, const char *p)
{
        unsigned char to[256];

        FILE *fp = fopen (private_key, "r");
        if (!fp) {
		g_print ("not open\n");
                return NULL;
        }

        int padding = RSA_PKCS1_PADDING;

        RSA *rsa = PEM_read_RSAPrivateKey (fp, NULL, NULL, NULL);
        int ll = 0;
        unsigned char *hex = convert_data_to_hex (p, &ll);
	for (int i = 0; i < ll; i++) {
//		g_print ("%02x", hex[i]);
	}
//	g_print ("\n");

        long int encrypted_length = RSA_private_decrypt (ll, (const unsigned char *) hex, to, rsa, padding);
        free (hex);
	if (encrypted_length == -1) {
        	RSA_free (rsa);
        	fclose (fp);
		return NULL;
	}

        RSA_free (rsa);
        fclose (fp);
	char *cc = calloc (encrypted_length + 1, 1);
	strncpy (cc, to, encrypted_length);

        return cc;//to_print_hex (to, encrypted_length);
}

static void getting_file (MainWindow *self)
{
	json_reader_read_member (self->reader, "from");
	JsonNode *jfrom = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "filename");
	JsonNode *jname = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "ckey");
	JsonNode *jckey = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "ivec");
	JsonNode *jivec = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "data");
	JsonNode *jdata = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "pos");
	JsonNode *jpos = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "size");
	JsonNode *jsize = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	json_reader_read_member (self->reader, "index");
	JsonNode *jindex = json_reader_get_value (self->reader);
	json_reader_end_member (self->reader);

	const char *from = json_node_get_string (jfrom);
	const char *filename = json_node_get_string (jname);
	const char *eckey = json_node_get_string (jckey);
	const char *eivec = json_node_get_string (jivec);
	const char *data = json_node_get_string (jdata);
	size_t pos = json_node_get_int (jpos);
	size_t size_buf = json_node_get_int (jsize);
	int index = json_node_get_int (jindex);

	struct _gd *gd = calloc (1, sizeof (struct _gd));
	gd->from = strdup (from);
	gd->filename = strdup (filename);
	gd->eckey = strdup (eckey);
	gd->eivec = strdup (eivec);
	gd->data = strdup (data);
	gd->pos = pos;
	gd->size_buf = size_buf;
	gd->index = index;
	gd->filled = 1;
	g_mutex_lock (&self->mutex);
	if (self->gd->start == NULL) {
		self->gd->start = gd;
	} else {
		struct _gd *g = self->gd->start;
		while (g->next) {
			g = g->next;
		}
		g->next = gd;
	}
	g_mutex_unlock (&self->mutex);

}

static gpointer receive_handler_cb (gpointer user_data)
{
	MainWindow *self = MAIN_WINDOW (user_data);
	while (1)
	{
		GError *error = NULL;
		size_t readed = g_input_stream_read (self->igio, self->buf, TOTAL_SIZE, NULL, &error);

  		if (error) {
			g_print ("error result read: %s\n", error->message);
			g_error_free (error);
			continue;
		}
		self->buf[readed] = 0;

		JsonParser *parser = json_parser_new ();
		if (!json_parser_load_from_data (parser, self->buf, -1, &error)) {
			if (error) {
				g_print ("load from data: %s\n", error->message);
				g_error_free (error);
				return;
			}
		}



		JsonReader *reader = json_reader_new (json_parser_get_root (parser));
		json_reader_read_member (reader, "type");
		JsonNode *jtype = json_reader_get_value (reader);
		json_reader_end_member (reader);
		const char *type = json_node_get_string (jtype);

		if (!strncmp (type, "storage_files", 14)) {
			self->reader = reader;
			fill_file_storage (self);
			goto end;
		}
		if (!strncmp (type, "getting_file", 13)) {
			self->reader = reader;
			getting_file (self);
			goto end;
		}

		if (!strncmp (type, "all_users", 10)) {
			self->reader = reader;
			fill_arrays (self);
			goto end;
		}
		if (!strncmp (type, "status_online", 14)) {
			self->reader = reader;
			fill_arrays_new_status (self);
			goto end;
		}
		if (!strncmp (type, "handshake_answer", 17)) {
			self->reader = reader;
			handshake_answer_status (self);
			goto end;
		}
		if (!strncmp (type, "handshake_key", 14)) {
			self->reader = reader;
			handshake_key_save (self);
			goto end;
		}
		if (!strncmp (type, "message", 8)) {
			self->reader = reader;
			got_message (self);
			goto end;
		}
		if (!strncmp (type, "handshake_notice", 17)) {
			self->reader = reader;
			handshake_notice (self);
			goto end;
		}
end:
		g_object_unref (parser);
		g_object_unref (reader);
	}

	return NULL;
}

static JsonNode *build_json_get_list () {
  g_autoptr(JsonBuilder) builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "type");
  json_builder_add_string_value (builder, "get_list");
  json_builder_end_object (builder);

  JsonNode *node = json_builder_get_root (builder);
	return node;
}

static JsonNode *build_json_feed () {
  g_autoptr(JsonBuilder) builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "type");
  json_builder_add_string_value (builder, "feed");
  json_builder_end_object (builder);

  JsonNode *node = json_builder_get_root (builder);
	return node;
}

void main_window_feed (MainWindow *self)
{
	self->ogio = g_io_stream_get_output_stream (G_IO_STREAM (self->conn));

	JsonNode *node = build_json_feed ();

	g_autoptr(JsonGenerator) gen = json_generator_new ();
	json_generator_set_root (gen, node);
	gsize length;
	g_autofree char *buffer = json_generator_to_data (gen, &length);

	GError *error = NULL;

	g_output_stream_write (G_OUTPUT_STREAM (self->ogio),
			buffer,
			length,
			NULL,
			&error);
	if (error) {
		g_print ("error send: %s\n", error->message);
		g_error_free (error);
	}

  //g_object_unref (node);
}

void main_window_get_list_users (MainWindow *self)
{
	self->ogio = g_io_stream_get_output_stream (G_IO_STREAM (self->conn));

	JsonNode *node = build_json_get_list ();

	g_autoptr(JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, node);
  gsize length;
  g_autofree char *buffer = json_generator_to_data (gen, &length);

	GError *error = NULL;
	g_output_stream_write (G_OUTPUT_STREAM (self->ogio),
			buffer,
			length,
			NULL,
			&error);
	if (error) {
		g_print ("error send: %s\n", error->message);
	}

	//g_object_unref (node);
}

typedef enum {
	PROP_CONN = 1,
	PROP_NOTIFICATION,
	PROP_APPLICATION,
	N_PROPERTIES
} MainWindowProperty;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void main_window_set_property (GObject *object,
		guint property_id,
		const GValue *value,
		GParamSpec *pspec)
{
	MainWindow *self = MAIN_WINDOW (object);

	switch ((MainWindowProperty) property_id) {
		case PROP_CONN:
      if (self->conn) g_object_unref (self->conn);
      if (self->igio) g_io_stream_close (self->gio, NULL, NULL);

			self->conn = g_value_get_object (value);
			self->igio = g_io_stream_get_input_stream (G_IO_STREAM (self->conn));
			self->ogio = g_io_stream_get_output_stream (G_IO_STREAM (self->conn));
			if (self->thread_receive) g_thread_unref (self->thread_receive);
			self->thread_receive = g_thread_new ("receive", receive_handler_cb, self);
			break;
		case PROP_NOTIFICATION:
			self->notification = g_value_get_object (value);
			break;
		case PROP_APPLICATION:
			self->app = g_value_get_object (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static const char *styles =
"window#main { background-color: #fafafa; }"
"headerbar#main { background: #fcfcfc; }"
"frame#list { background-color: #000000; }"
"list#list_users { background-color: #d9d9d9; font-size: 16px; }"
"list#list_users row:selected { background-color: #9ecaff; min-height: 64px; font-size: 16px; }"
"list#list_files { background-color: #d9d9d9; margin: 32px; }"
"image#header { min-width: 32px; min-height: 32px; }"
;

static void main_window_class_init (MainWindowClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = main_window_set_property;

	obj_properties[PROP_CONN] = g_param_spec_object (
			"conn",
			"Conn",
			"Connection",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_NOTIFICATION] = g_param_spec_object (
			"notification",
			"Notification",
			"Notification",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_APPLICATION] = g_param_spec_object (
			"app",
			"app",
			"app",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);

	g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

static int switcher_dec;
static int switcher_inc;

GtkWidget *main_window_get_paned (MainWindow *self)
{
  return self->main_pane;
}

static gboolean handler_left_pane_dec_cb (gpointer user_data)
{
	if (switcher_inc) {
		switcher_dec = 0;
		return G_SOURCE_REMOVE;
	}

	MainWindow *self = (MainWindow *) user_data;

	int pos = gtk_paned_get_position (GTK_PANED (self->main_pane));
	if ((pos - 30) <= 0) {
		gtk_paned_set_position (GTK_PANED (self->main_pane), 0);
		switcher_dec = 0;
		return G_SOURCE_REMOVE;
	} else {
		pos -= 30;
		gtk_paned_set_position (GTK_PANED (self->main_pane), pos);
		return G_SOURCE_CONTINUE;
	}
}

#define WIDTH_USER_LIST        342

static gboolean handler_left_pane_inc_cb (gpointer user_data)
{
	if (switcher_dec) {
		switcher_inc = 0;
		return G_SOURCE_REMOVE;
	}

	MainWindow *self = (MainWindow *) user_data;

	int pos = gtk_paned_get_position (GTK_PANED (self->main_pane));
	if ((pos + 30) >= WIDTH_USER_LIST) {
		gtk_paned_set_position (GTK_PANED (self->main_pane), WIDTH_USER_LIST);
		switcher_inc = 0;
		return G_SOURCE_REMOVE;
	} else {
		pos += 30;
		gtk_paned_set_position (GTK_PANED (self->main_pane), pos);
		return G_SOURCE_CONTINUE;
	}
}

#define OFF       0
#define ON        1

static JsonNode *build_json_handshake (const char *name, const int status)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "type");
  json_builder_add_string_value (builder, "handshake");

  json_builder_set_member_name (builder, "to_name");
  json_builder_add_string_value (builder, name);

  json_builder_set_member_name (builder, "status");
  json_builder_add_int_value (builder, status);



	if (status) {
		generate_keys_for (name);
		char path[256];
		snprintf (path, 256, "%s/%s/pub.pem", root_app, name);
		GFile *file = g_file_new_for_path (path);
		char *content = NULL;
		gsize length = 0;
		GError *error = NULL;
		g_file_load_contents (file,
				NULL,
				&content,
				&length,
				NULL,
				&error);
		if (error) {
			g_print ("read key pub content: %s\n", error->message);
			g_error_free (error);
		}

		if (content) {
      json_builder_set_member_name (builder, "key");
      json_builder_add_string_value (builder, content);
			g_free (content);
		}
	}

  json_builder_end_object (builder);

  JsonNode *node = json_builder_get_root (builder);
	return node;
}

static void send_status_handshake (MainWindow *self, const char *name, const int status)
{
  (void) self;
	JsonNode *node = build_json_handshake (name, status);

  g_autoptr(JsonGenerator) gen = json_generator_new ();
  json_generator_set_root (gen, node);

  gsize length;
	g_autofree char *data = json_generator_to_data (gen, &length);

	g_output_stream_write (
			G_OUTPUT_STREAM (self->ogio),
			data,
			length,
			NULL,
			NULL
			);

}

static void handshake_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	MainWindow *self = (MainWindow *) user_data;

	int status = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->list_users));
	if (!row) return;

	GtkWidget *child = gtk_list_box_row_get_child (row);
	const char *name = user_item_get_name (USER_ITEM (child));

	if (!status) {
		send_status_handshake (self, name, OFF);
	} else {
		send_status_handshake (self, name, ON);
	}
}

static void left_pane_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	MainWindow *self = (MainWindow *) user_data;

	int status = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

	if (status) {
		switcher_inc = 1;
		g_timeout_add (30, handler_left_pane_inc_cb, self);
	} else {
		switcher_dec = 1;
		g_timeout_add (30, handler_left_pane_dec_cb, self);
	}
}

int list_users_sort_func_cb (
		GtkListBoxRow *row1,
		GtkListBoxRow *row2,
		gpointer user_data)
{
  (void) user_data;

	GtkWidget *child1 = gtk_list_box_row_get_child (row1);
	GtkWidget *child2 = gtk_list_box_row_get_child (row2);

	int status1 = user_item_get_status (USER_ITEM (child1));
	int status2 = user_item_get_status (USER_ITEM (child2));

	if (status1 > status2) return -1;
	if (status1 < status2) return 1;
	return 0;
}


void main_window_play_new_message (MainWindow *self)
{
	gst_element_set_state (self->new_message, GST_STATE_READY);
	gst_element_seek_simple (self->new_message, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0L);
	gst_element_set_state (self->new_message, GST_STATE_PLAYING);
}

static gboolean storage_window_close_request_cb (GtkWindow *window,
		gpointer user_data)
{
	gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
	return TRUE;
}

static void create_storage_window (MainWindow *self)
{
	self->storage_window = g_object_new (GTK_TYPE_WINDOW,
			"default-width", 300,
			"default-height", 600,
			NULL);
	self->scroll_list_box = gtk_scrolled_window_new ();
	self->list_box_storage = gtk_list_box_new ();
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scroll_list_box), self->list_box_storage);
	gtk_window_set_child (GTK_WINDOW (self->storage_window), self->scroll_list_box);
	gtk_widget_set_name (self->list_box_storage, "list_files");

	g_signal_connect (self->storage_window, "close-request", G_CALLBACK (storage_window_close_request_cb), self);
}

static void get_list_storage_files (MainWindow *self, const char *name)
{
  	g_autoptr(JsonBuilder) builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "storage_files");
	json_builder_set_member_name (builder, "from");
	json_builder_add_string_value (builder, name);
	json_builder_end_object (builder);

	JsonNode *node = json_builder_get_root (builder);
	self->ogio = g_io_stream_get_output_stream (G_IO_STREAM (self->conn));

	g_autoptr(JsonGenerator) gen = json_generator_new ();
	json_generator_set_root (gen, node);
	gsize length;
	g_autofree char *buffer = json_generator_to_data (gen, &length);

	GError *error = NULL;

	g_output_stream_write (G_OUTPUT_STREAM (self->ogio),
			buffer,
			length,
			NULL,
			&error);
	if (error) {
		g_print ("error send: %s\n", error->message);
		g_error_free (error);
	}
}

static void storage_button_clicked_cb (GtkButton *button, gpointer user_data)
{
	MainWindow *self = MAIN_WINDOW (user_data);

	gtk_widget_set_visible (self->storage_window, TRUE);

	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (self->list_users));
	if (!row) return;

	GtkWidget *sel_child = gtk_list_box_row_get_child (row);
	const char *name = user_item_get_name (USER_ITEM (sel_child));

	while (1)
	{
		GtkListBoxRow *r = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list_box_storage), 0);
		if (r != NULL)
		{
			gtk_list_box_remove (GTK_LIST_BOX (self->list_box_storage), r);
		} else {
			break;
		}
	}
	self->index_storage = 0;

	get_list_storage_files (self, name);
}

struct xi {
	double progress;
	int index;
	MainWindow *self;
};

static gboolean set_progress_idle (gpointer user_data)
{
	struct xi *xi = (struct xi *) user_data;
	MainWindow *self = xi->self;
	GtkListBoxRow *r = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list_box_storage), xi->index);
	GtkWidget *child = gtk_list_box_row_get_child (r);
	g_object_set (child,
			"progress", xi->progress,
			NULL);
	free (xi);

	return G_SOURCE_REMOVE;
}

static gpointer thread_get_file (gpointer user_data)
{
	MainWindow *self = MAIN_WINDOW (user_data);

	while (1)
	{
		if (self->gd->start == NULL) {
			g_usleep (1000);
			continue;
		}

		const char *from = self->gd->start->from;
		const char *filename = self->gd->start->filename;
		const char *eckey = self->gd->start->eckey;
		const char *eivec = self->gd->start->eivec;
		const char *data = self->gd->start->data;
		int index = self->gd->start->index;
		size_t pos = self->gd->start->pos;
		size_t size_buf = self->gd->start->size_buf;


		char private_key[256];
		snprintf (private_key, 256, "%s/%s/key.pem", root_app, from);

		char *file_path[256];
		snprintf (file_path, 256, "%s", download_app);
		if (access (file_path, F_OK)) {
			mkdir (file_path, 0755);
		}
		snprintf (file_path, 256, "%s/%s", download_app, from);
		if (access (file_path, F_OK)) {
			mkdir (file_path, 0755);
		}
		snprintf (file_path, 256, "%s/%s/%s", download_app, from, filename);

		char *ckey = _rsa_decrypt (private_key, eckey);
		char *ivec = _rsa_decrypt (private_key, eivec);

		size_t length;
		unsigned char *hex = convert_data_to_hex (data, &length);
		unsigned char *s = hex;
		int num = 0;
		FILE *afp;
		if (pos == 0) {
			afp = fopen (file_path, "w");
		} else {
			afp = fopen (file_path, "a");
		}
		unsigned char *b = malloc (16 * 450 + 1);
		int plaintext_len;
		EVP_CIPHER_CTX *ctx;
		int len;
		ctx = EVP_CIPHER_CTX_new ();
		EVP_DecryptInit_ex (ctx, EVP_aes_128_cfb128 (), NULL, ckey, ivec);
		EVP_DecryptUpdate (ctx, b, &len, hex, length);
		plaintext_len = len;
		EVP_DecryptFinal_ex (ctx, b + len, &len);
		plaintext_len += len;
		EVP_CIPHER_CTX_free (ctx);
		int ret = fwrite (b, 1, plaintext_len, afp);
end:
		free (hex);
		fclose (afp);
		free (ckey);
		free (ivec);

		g_mutex_lock (&self->mutex);

		struct _gd *g = self->gd->start;
		self->gd->start = self->gd->start->next;

		g_mutex_unlock (&self->mutex);

		g_free (g->from);
		g_free (g->filename);
		g_free (g->eckey);
		g_free (g->eivec);
		g_free (g->data);
		g_free (g);

		struct xi *xi = calloc (1, sizeof (struct xi));
		xi->progress = (double) (pos * 100) / (double) (size_buf) / (double) (100.0);
		xi->self = self;
		xi->index = index;

		g_idle_add (set_progress_idle, xi);
	}
}

static void main_window_init (MainWindow *self)
{
	self->gd = calloc (1, sizeof (struct gd));
	g_mutex_init (&self->mutex);
	self->get_file = g_thread_new ("get_file", thread_get_file, self);
	create_storage_window (self);
  GFile *ff = g_file_new_for_uri ("resource:///io/github/xverizex/nem_desktop/in_message.mp3");

	GError *error = NULL;
  char ppath[512];
  snprintf (ppath, 512, "%s/in_message.mp3", root_sounds);
  if (access (ppath, F_OK)) {
    g_autofree char *content = NULL;
    gsize length;
    if (!g_file_load_contents (ff,
                               NULL,
                               &content,
                               &length,
                               NULL,
                               &error))
      {
        if (error)
          {
            g_print ("error resource load in_message: %s\n", error->message);
            g_error_free (error);
            error = NULL;
          }
      } else {
        GFile *file = g_file_new_for_path (ppath);
        GFileOutputStream *out = g_file_create (file,
                                                G_FILE_CREATE_NONE,
                                                NULL,
                                                &error);
        if (error) {
          g_print ("create file in_message.: %s\n", error->message);
          g_error_free (error);
          error = NULL;
        }
               g_output_stream_write (G_OUTPUT_STREAM (out),
                                      content,
                                      length,
                                      NULL,
                                      &error);
        if (error) {
          g_print ("error write in_message: %s\n", error->message);
          g_error_free (error);
          error = NULL;
        }
        g_output_stream_close (G_OUTPUT_STREAM (out), NULL, NULL);

      }

  }
  snprintf (ppath, 512,"filesrc location=%s/in_message.mp3 "
			"! mpegaudioparse ! mpg123audiodec ! audioconvert ! audioresample ! autoaudiosink",
            root_sounds);


	self->new_message = gst_parse_launch ( ppath, &error);
	if (error) {
		g_print ("error parse gst file in_message: %s\n", error->message);
		g_error_free (error);
	}
	self->buf = malloc (TOTAL_SIZE);

	self->display = gdk_display_get_default ();
	self->provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (self->provider, styles, -1);
	gtk_style_context_add_provider_for_display (self->display, (GtkStyleProvider *) self->provider, GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_widget_set_name ((GtkWidget *) self, "main");
	gtk_window_set_title (GTK_WINDOW (self), "SECURE CHAT");

	self->header_bar = gtk_header_bar_new ();
	gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR (self->header_bar), ":minimize,maximize,close");
	gtk_header_bar_set_show_title_buttons (GTK_HEADER_BAR (self->header_bar), TRUE);
	gtk_window_set_titlebar (GTK_WINDOW (self), self->header_bar);
	gtk_widget_set_name (self->header_bar, "main");

	self->main_pane = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	self->frame_list = gtk_frame_new (NULL);
	self->frame_chat = gtk_frame_new (NULL);
	g_object_set (self->main_pane, "vexpand", TRUE, "hexpand", TRUE, NULL);
	g_object_set (self->frame_chat, "vexpand", TRUE, "hexpand", TRUE, NULL);

	gtk_widget_set_size_request (self->frame_list, WIDTH_USER_LIST, -1);
	gtk_widget_set_size_request (self->frame_chat, 1024 - WIDTH_USER_LIST, -1);
	gtk_widget_set_visible (self->main_pane, TRUE);
	gtk_widget_set_visible (self->frame_list, TRUE);
	gtk_widget_set_visible (self->frame_chat, TRUE);
	gtk_widget_set_name (self->frame_list, "list");
	gtk_paned_set_start_child (GTK_PANED (self->main_pane), self->frame_list);
	gtk_paned_set_resize_start_child (GTK_PANED (self->main_pane), FALSE);
	gtk_paned_set_shrink_start_child (GTK_PANED (self->main_pane), TRUE);
	gtk_paned_set_end_child (GTK_PANED (self->main_pane), self->frame_chat);
	gtk_paned_set_resize_end_child (GTK_PANED (self->main_pane), TRUE);
	gtk_paned_set_shrink_end_child (GTK_PANED (self->main_pane), TRUE);
	gtk_window_set_child (GTK_WINDOW (self), self->main_pane);

	self->scroll_list = gtk_scrolled_window_new ();
	gtk_frame_set_child (GTK_FRAME (self->frame_list), self->scroll_list);

	self->list_users = gtk_list_box_new ();
	g_signal_connect (self->list_users, "row-selected", G_CALLBACK (user_item_row_selected_cb), self);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scroll_list), self->list_users);
	gtk_widget_set_name (GTK_WIDGET (self->list_users), "list_users");
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_users),
			list_users_sort_func_cb,
			NULL,
			NULL
			);

	self->left_pane_button = gtk_toggle_button_new ();
	self->image_header_left_pane = g_object_new (GTK_TYPE_IMAGE,
                                               "resource", "/io/github/xverizex/nem_desktop/left-pane.svg",
                                               NULL);

	gtk_button_set_child (GTK_BUTTON (self->left_pane_button), self->image_header_left_pane);
	gtk_widget_set_name (self->image_header_left_pane, "header");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->left_pane_button), TRUE);
	g_signal_connect (self->left_pane_button, "clicked", G_CALLBACK (left_pane_button_clicked_cb), self);

	self->handshake_button = gtk_toggle_button_new ();
  self->image_handshake = g_object_new (GTK_TYPE_IMAGE,
                                        "resource", "/io/github/xverizex/nem_desktop/handshake.svg",
                                        "icon-size", GTK_ICON_SIZE_NORMAL,
                                        NULL);

	gtk_button_set_child (GTK_BUTTON (self->handshake_button), self->image_handshake);
	gtk_widget_set_name (self->image_handshake, "header");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->handshake_button), FALSE);
	g_signal_connect (self->handshake_button, "clicked", G_CALLBACK (handshake_button_clicked_cb), self);

	self->storage_button = gtk_button_new ();
	self->image_storage = g_object_new (GTK_TYPE_IMAGE,
			"resource", "/io/github/xverizex/nem_desktop/storage.svg",
			"icon-size", GTK_ICON_SIZE_NORMAL,
			NULL);
	gtk_button_set_child (GTK_BUTTON (self->storage_button), self->image_storage);
	gtk_widget_set_name (self->image_storage,  "header");
	g_signal_connect (self->storage_button, "clicked", G_CALLBACK (storage_button_clicked_cb), self);

	self->menu_button = g_object_new (GTK_TYPE_MENU_BUTTON,
                                    "icon-name", "open-menu",
                                    NULL);

	self->menu = g_menu_new ();
	g_menu_append (self->menu, "REGISTER", "app.register");
	g_menu_append (self->menu, "LOGIN", "app.login");
	g_menu_append (self->menu, "QUIT", "app.quit");

	gtk_menu_button_set_menu_model ( GTK_MENU_BUTTON (self->menu_button), (GMenuModel *) self->menu);

	gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), self->menu_button);

	gtk_header_bar_pack_start (GTK_HEADER_BAR (self->header_bar), self->left_pane_button);
	gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), self->handshake_button);
	gtk_header_bar_pack_end (GTK_HEADER_BAR (self->header_bar), self->storage_button);

}
