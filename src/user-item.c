/* user-item.c
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

#include "user-item.h"
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <json-glib/json-glib.h>
#include <sys/stat.h>
#include "message-item.h"
#include "main-window.h"

extern char *root_app;

struct _UserItem {
	GtkFrame parent_instance;

	char *icon;
	char *name;
	int status;
	int blink;
	int blink_handshake;


	GdkDisplay *display;
	GtkCssProvider *provider;

	GtkWidget *box;
	GtkWidget *iconw;
	GtkWidget *namew;
	GtkWidget *statusw;
	GtkWidget *handbutton;
	GtkWidget *frame_chat;
	GtkWidget *box_frame_chat;
	GtkWidget *scroll;
	GtkWidget *box_scroll;
	GtkWidget *box_for_entry;
	GtkWidget *entry_input_text;
	GtkWidget *button_entry_file;
	GOutputStream *ogio;
	GNotification *notification;
	GtkApplication *app;
	gboolean handshaked;
	GtkWidget *main_window;
	GtkWidget *window_add_file;
	GtkWidget *frame_add_file;
	GtkWidget *entry_filename;
	GtkWidget *entry_path;
	GtkWidget *button_path;
	GtkWidget *button_upload;
	GtkWidget *box_frame_add_file;
	GtkWidget *native;
	GtkWidget *progress;
};

G_DEFINE_TYPE (UserItem, user_item, GTK_TYPE_FRAME)

typedef enum {
	PROP_ICON = 1,
	PROP_NAME,
	PROP_STATUS,
	PROP_BLINK,
	PROP_BLINK_HANDSHAKE,
	PROP_HANDSHAKED,
	PROP_HANDBUTTON,
	PROP_FRAME_CHAT,
	PROP_OGIO,
	PROP_NOTIFICATION,
	PROP_APP,
	PROP_MAIN_WINDOW,
	N_PROPERTIES
} UserItemProperty;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static const char *styles =
"@keyframes blinks { "
"	from { "
"		background-color: #b0bfd1; "
"	}"
"	50% {"
"		background-color: #11bfc2; "
"	}"
"	to {"
"		background-color: #b0bfd1; "
"	}"
"}"

"@keyframes blinks_handshake { "
"	from { "
"		background-color: #b0bfd1; "
"	}"
"	50% {"
"		background-color: #01944d; "
"	}"
"	to {"
"		background-color: #b0bfd1; "
"	}"
"}"
"frame#noblink { background-color: #b0bfd1; }"
"frame#blink { background-color: #b0bfd1; animation-name: blinks; "
"		animation-play-state: running; "
"		animation-duration: 1s;"
"		animation-iteration-count: infinite;"
"		animation-timing-function: linear; }"

"frame#noblink_handshake { background-color: #b0bfd1; }"
"frame#blink_handshake { background-color: #b0bfd1; animation-name: blinks_handshake; "
"		animation-play-state: running; "
"		animation-duration: 1s;"
"		animation-iteration-count: infinite;"
"		animation-timing-function: linear; }"
""
"label#item { color: #000000; font-size: 21px; }"
"image#item { min-width: 48px; min-height: 48px; margin: 8px; }"
"frame#msg { background-color: #b0bfd1; margin-top: 8px; margin-bottom: 8px; margin-left: 16px; margin-right: 16px; }"
"window#add_file { background-color: #ffffff; } "
"frame#add_file { background-color: #8c8c8c; margin: 48px; }"
;



void user_item_set_chat (UserItem *self) {
	GtkWidget *child = gtk_frame_get_child (GTK_FRAME (self->frame_chat));
	if (child) g_object_ref (child);
	gtk_frame_set_child (GTK_FRAME (self->frame_chat), self->box_frame_chat);
}

void user_item_add_message (UserItem *self, const char *msg, int me, const char *from, const int show_notification) {
	GtkWidget *frame = gtk_frame_new (NULL);
	GtkWidget *text_view = g_object_new (MESSAGE_TYPE_ITEM, NULL);

	g_object_set (frame,
			"width-request", 400,
			"halign", me ? GTK_ALIGN_START: GTK_ALIGN_END,
			NULL);
	g_object_set (text_view,
			"text", msg,
			"max_width", 400,
      "main_window", self->main_window,
      "app", self->app,
			NULL);

	gtk_widget_set_name (frame, "msg");
	gtk_frame_set_child (GTK_FRAME (frame), text_view);

	gtk_box_append ( GTK_BOX (self->box_scroll), frame);

	GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroll));
	gtk_adjustment_set_value ( vadj, gtk_adjustment_get_upper ( vadj ) );

	if (me == 0 && show_notification) {
		g_notification_set_title (G_NOTIFICATION (self->notification),
				from
				);
		g_notification_set_body (G_NOTIFICATION (self->notification),
				msg
				);
		g_application_send_notification (G_APPLICATION (self->app),
				NULL,
				G_NOTIFICATION (self->notification)
				);
	}
}

static void send_message_to ( const char *name, char *path, const char *buffer, UserItem *self) {
	unsigned char *to = calloc (4096, 1);
	if (!to) return;

	FILE *fp = fopen (path, "r");
	if (!fp) {
		free (to);
		return;
	}

	int padding = RSA_PKCS1_PADDING;

	int buffer_len = strlen (buffer);
	RSA *rsa = PEM_read_RSA_PUBKEY (fp, NULL, NULL, NULL);

	int encrypted_length = RSA_public_encrypt (buffer_len, (const unsigned char *) buffer, to, rsa, padding);
	char *buf = calloc (encrypted_length * 2 + 1, 1);
	if (!buf) {
		fclose (fp);
		free (to);
		return;
	}
	int n = 0;
	char *s = buf;
	for (int i = 0; i < encrypted_length; i++) {
		sprintf (s, "%02x%n", to[i], &n);
		s += n;
	}

  JsonBuilder *builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "type");
  json_builder_add_string_value (builder, "message");
  json_builder_set_member_name (builder, "to");
  json_builder_add_string_value (builder, name);
  json_builder_set_member_name (builder, "data");
  json_builder_add_string_value (builder, buf);
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
  free (data);
	RSA_free (rsa);
	free (to);
	free (buf);
}

static void entry_input_text_cb (GtkEntry *entry, gpointer user_data) {
	UserItem *self = USER_ITEM (user_data);

	GtkEntryBuffer *buffer = gtk_entry_get_buffer (entry);
	const char *text = gtk_entry_buffer_get_text (buffer);
	if (strlen (text) == 0) return;

	const char *name = user_item_get_name (USER_ITEM (self));

	char path[256];
	snprintf (path, 256, "%s/%s/crypto.pem", root_app, name);


	if (access (path, F_OK)) {

		g_notification_set_body (G_NOTIFICATION (self->notification),
				"Not found public key from user."
				);
		g_application_send_notification (G_APPLICATION (self->app),
				NULL,
				G_NOTIFICATION (self->notification)
				);
		return;
	}

	send_message_to (name, path, text, self);
	user_item_add_message (self, text, 1, NULL, 0);
	gtk_entry_buffer_set_text (GTK_ENTRY_BUFFER (buffer), "", -1);

	GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroll));
	gtk_adjustment_set_value ( vadj, gtk_adjustment_get_upper ( vadj ) );

//	main_window_play_new_message (MAIN_WINDOW (self->main_window));
#if 0
	if (scroll_event == 0) {
		gtk_adjustment_set_value (vadj, gtk_adjustment_get_upper (vadj));
	} else {
		gtk_adjustment_set_value (vadj, gtk_adjustment_get_value (vadj));
	}
#endif
}

static void button_path_response_cb (GtkNativeDialog *native,
		int response,
		gpointer user_data)
{
	UserItem *self = USER_ITEM (user_data);

	if (response == GTK_RESPONSE_ACCEPT)
	{
		GtkFileChooser *chooser = GTK_FILE_CHOOSER (native);
		GFile *file = gtk_file_chooser_get_file (chooser);
		const char *path = g_file_get_path (file);
		GtkEntryBuffer *buffer = gtk_entry_get_buffer (GTK_ENTRY (self->entry_path));
		gtk_entry_buffer_set_text (buffer, path, -1);
	}

	g_object_unref (native);
}

static void button_path_clicked_cb (GtkButton *button, gpointer user_data)
{
	UserItem *self = USER_ITEM (user_data);
	self->native = gtk_file_chooser_native_new ("PATH",
			self->window_add_file,
			GTK_FILE_CHOOSER_ACTION_OPEN,
			"Open",
			"Cancel");
	g_signal_connect (self->native, "response", G_CALLBACK (button_path_response_cb), self);
	gtk_native_dialog_show (GTK_NATIVE_DIALOG (self->native));
}

static char *to_print_hex (unsigned char *data, const long int length)
{
	char *buf = calloc (length * 2 + 1, 1);
	if (!buf) {
		return NULL;
	}
	int n = 0;
	char *s = buf;
	for (int i = 0; i < length; i++) {
		sprintf (s, "%02x%n", data[i], &n);
		s += n;
	}

	return buf;
}

static char *_rsa_encrypt (const char *path_key, const char *p)
{
	unsigned char to[256];

	FILE *fp = fopen (path_key, "rb");
	if (!fp) {
		return NULL;
	}

	int padding = RSA_PKCS1_PADDING;

	RSA *rsa = PEM_read_RSA_PUBKEY (fp, NULL, NULL, NULL);

	long int encrypted_length = RSA_public_encrypt (16, (const unsigned char *) p, to, rsa, padding);

	RSA_free (rsa);
	fclose (fp);

	return to_print_hex (to, encrypted_length);
}
static void build_and_send_block_data (const char *name, 
		const char *filename, 
		const char *encrypted_data, 
		const char *buf_ckey,
		const char *buf_ivec,
		const int is_start,
		UserItem *self)
{
	JsonBuilder *builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "file_add");
	json_builder_set_member_name (builder, "to");
	json_builder_add_string_value (builder, name);
	json_builder_set_member_name (builder, "filename");
	json_builder_add_string_value (builder, filename);
	json_builder_set_member_name (builder, "ckey");
	json_builder_add_string_value (builder, buf_ckey);
	json_builder_set_member_name (builder, "ivec");
	json_builder_add_string_value (builder, buf_ivec);
	json_builder_set_member_name (builder, "data");
	json_builder_add_string_value (builder, encrypted_data);
	json_builder_set_member_name (builder, "is_start");
	json_builder_add_int_value (builder, is_start);
	json_builder_end_object (builder);

	long int length;
	JsonNode *node = json_builder_get_root (builder);
	JsonGenerator *gen = json_generator_new ();
	json_generator_set_root (gen, node);
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

struct dtf {
	GtkWidget *progress;
	char *ckey;
	char *ivec;
	char *eckey;
	char *eivec;
	FILE *fp;
	AES_KEY key;
	int is_start;
	size_t size;
	size_t pos;
	char *filename;
	char *name;
	UserItem *self;
};

struct vp {
	GtkWidget *progress;
	double fraction;
	struct dtf *dtf;
};

static gboolean set_fraction (gpointer user_data)
{
	struct vp *vp = (struct vp *) user_data;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (vp->progress), vp->fraction);
	if (vp->fraction == 1.0) {
		struct dtf *dtf = vp->dtf;
		free (dtf->ckey);
		free (dtf->ivec);
		free (dtf->eckey);
		free (dtf->eivec);
		fclose (dtf->fp);
		free (dtf);
		free (vp);
	}
	return FALSE;
}

static gpointer dtf_send_file (gpointer user_data)
{
	struct dtf *dtf = (struct dtf *) user_data;

	unsigned char indata[AES_BLOCK_SIZE];
	unsigned char outdata[AES_BLOCK_SIZE];


	const int START = 0;
	const int CONTINUE = 1;
	int num = 0;
	struct vp *vp = calloc (1, sizeof (struct vp));
	vp->progress = dtf->progress;
	vp->dtf = dtf;
	unsigned char *cipher = malloc (16 * 450);
	unsigned char *plain = malloc (16 * 450);
	int iter = 0;
	int buf_index = 0;
	int readed;
	int len;
	int cipher_len;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new ();
	dtf->is_start = 0;
	while (1)
	{
		EVP_EncryptInit_ex (ctx, EVP_aes_128_cfb128 (), NULL, dtf->ckey, dtf->ivec);
		int readed = fread (plain, 1, 16 * 450, dtf->fp);
		if (readed <= 0) break;
		EVP_EncryptUpdate (ctx, cipher, &len, plain, readed);
		cipher_len = len;
		EVP_EncryptFinal_ex (ctx, cipher + len, &len);
		cipher_len += len;

		char *encrypted_data = to_print_hex (cipher, cipher_len);
		build_and_send_block_data (dtf->name, dtf->filename, encrypted_data, dtf->eckey, dtf->eivec, dtf->is_start, dtf->self);
		free (encrypted_data);
		dtf->pos += readed;
		vp->fraction = (double) (dtf->pos * 100) / (double) (dtf->size) / (double) (100.0);
		if (vp->fraction > 1.0) vp->fraction = 1.0;
		g_idle_add (set_fraction, vp);
		dtf->is_start = 1;

	}
	EVP_CIPHER_CTX_free (ctx);
	free (cipher);
	free (plain);

	return NULL;
}

static void send_file (const char *filename, const char *name, char *path_crypto, const unsigned char *path_file, UserItem *self) {


	unsigned char *ckey = calloc (17, 1);
	unsigned char *ivec = calloc (17, 1);
	unsigned char *keys[] =
	{
		&ckey[0],
		&ivec[0]
	};
	for (int ibuf = 0; ibuf < 2; ibuf++)
	{
		unsigned char *s = keys[ibuf];

		for (int i = 0; i < 16; i++)
		{
			s[i] = random () % (126 - 48) + 48;
		}
		s[16] = 0;
	}

	char *buf_ckey_encrypted = _rsa_encrypt (path_crypto, ckey);
	char *buf_ivec_encrypted = _rsa_encrypt (path_crypto, ivec);

	struct stat st;
	stat (path_file, &st);

	FILE *fp = fopen (path_file, "r");

	struct dtf *dtf = calloc (1, sizeof (struct dtf));
	dtf->eckey = buf_ckey_encrypted;
	dtf->eivec = buf_ivec_encrypted;
	dtf->progress = self->progress;
	dtf->fp = fp;
	dtf->ckey = ckey;
	dtf->ivec = ivec;
	dtf->size = st.st_size;
	dtf->pos = 0;
	dtf->self = self;
	dtf->filename = strdup (filename);
	dtf->name = strdup (name);
	AES_set_encrypt_key (ckey, 128, &dtf->key);

	GThread *thread = g_thread_new ("dtf_send_files", dtf_send_file, dtf);
}

static void button_upload_clicked_cb (GtkButton *button, gpointer user_data)
{
	UserItem *self = USER_ITEM (user_data);

	GtkEntryBuffer *buffer_filename = gtk_entry_get_buffer (GTK_ENTRY (self->entry_filename));
	GtkEntryBuffer *buffer_path = gtk_entry_get_buffer (GTK_ENTRY (self->entry_path));
	const char *filename = gtk_entry_buffer_get_text (buffer_filename);
	const char *path = gtk_entry_buffer_get_text (buffer_path);

	if (!strlen (filename) || !strlen (path)) 
	{
		g_notification_set_body (self->notification, "upload: fill all entries");
		g_application_send_notification (self->app, NULL, self->notification);
		return;
	}

	const char *name = user_item_get_name (USER_ITEM (self));

	char path_crypto[256];
	snprintf (path_crypto, 256, "%s/%s/crypto.pem", root_app, name);

	GError *error = NULL;

	send_file (filename, name, path_crypto, path, self);
}

static gboolean window_add_file_close_request (GtkWidget *w,
		gpointer user_data)
{
	gtk_widget_set_visible (w, FALSE);
	return TRUE;
}

static void create_window_add_file (UserItem *self) 
{
	self->window_add_file = gtk_window_new ();
	self->frame_add_file = gtk_frame_new (NULL);
	self->entry_filename = gtk_entry_new ();
	self->entry_path = gtk_entry_new ();
	self->button_path = gtk_button_new_with_label ("PATH");
	g_signal_connect (self->button_path, "clicked", G_CALLBACK (button_path_clicked_cb), self);
	self->button_upload = gtk_button_new_with_label ("UPLOAD");
	g_signal_connect (self->button_upload, "clicked", G_CALLBACK (button_upload_clicked_cb), self);
	self->box_frame_add_file = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *box_path = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append (GTK_BOX (box_path), self->entry_path);
	gtk_box_append (GTK_BOX (box_path), self->button_path);
	gtk_box_append (GTK_BOX (self->box_frame_add_file), self->entry_filename);
	gtk_box_append (GTK_BOX (self->box_frame_add_file), box_path);
	gtk_box_append (GTK_BOX (self->box_frame_add_file), self->button_upload);

	gtk_window_set_child (GTK_WINDOW (self->window_add_file), self->frame_add_file);
	gtk_frame_set_child (GTK_FRAME (self->frame_add_file), self->box_frame_add_file);

	gtk_widget_set_name (self->window_add_file, "add_file");
	gtk_widget_set_name (self->frame_add_file, "add_file");

	self->progress = gtk_progress_bar_new ();
	gtk_box_append (GTK_BOX (self->box_frame_add_file), self->progress);

	g_signal_connect (self->window_add_file, "close-request", G_CALLBACK (window_add_file_close_request), self);
}

static void button_entry_file_cb (GtkButton *button, gpointer user_data)
{
	UserItem *self = USER_ITEM (user_data);

	gtk_widget_set_visible (self->window_add_file, TRUE);
}

static void user_item_init (UserItem *self) {
	self->display = gdk_display_get_default ();
	self->provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (self->provider, styles, -1);
	gtk_style_context_add_provider_for_display (self->display, (GtkStyleProvider *) self->provider, GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_widget_set_name (GTK_WIDGET (self), "noblink");

	create_window_add_file (self);

	self->box_frame_chat = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	self->box_scroll = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	self->box_for_entry = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	self->button_entry_file = g_object_new (GTK_TYPE_BUTTON,
			"child", gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/add_file.svg"),
			NULL);
	g_signal_connect (self->button_entry_file, "clicked", G_CALLBACK (button_entry_file_cb), self);
	self->entry_input_text = gtk_entry_new ();
	g_signal_connect (self->entry_input_text, "activate", G_CALLBACK (entry_input_text_cb), self);
	self->scroll = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scroll), self->box_scroll);
	gtk_box_append (GTK_BOX (self->box_frame_chat), self->scroll);
	gtk_box_append (GTK_BOX (self->box_for_entry), self->button_entry_file);
	gtk_box_append (GTK_BOX (self->box_for_entry), self->entry_input_text);
	gtk_box_append (GTK_BOX (self->box_frame_chat), self->box_for_entry);
	g_object_set (self->box_scroll,
			"vexpand", TRUE,
			"hexpand", TRUE,
			NULL);
	g_object_set (self->scroll,
			"vexpand", TRUE,
			"hexpand", TRUE,
			NULL);
	g_object_set (self->entry_input_text,
			"hexpand", TRUE,
			NULL);

	self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	self->namew = gtk_label_new ("aaaaaaaaaaaaaaaa");
	gtk_widget_set_name (self->namew, "item");
	g_object_set (self->namew,
			"overflow", TRUE,
			NULL);

	gtk_frame_set_child (GTK_FRAME (self), self->box);

	self->iconw = gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/human_default.svg");
	gtk_widget_set_name (self->iconw, "item");

	self->statusw = gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/status_offline.svg");
	g_object_set (self->statusw, "halign", GTK_ALIGN_END, "hexpand", TRUE, NULL);
	gtk_widget_set_name (self->statusw, "item");


	gtk_box_append (GTK_BOX (self->box), self->iconw);

	gtk_box_append (GTK_BOX (self->box), self->namew);

	gtk_box_append (GTK_BOX (self->box), self->statusw);


	g_object_set (self, "hexpand", TRUE, NULL);
	g_object_set (self, "height-request", 64, NULL);

}

static void user_item_set_property (GObject *object,
		guint property_id,
		const GValue *value,
		GParamSpec *pspec) {
	UserItem *self = USER_ITEM (object);

	switch ((UserItemProperty) property_id) {
		case PROP_ICON:
			self->icon = g_value_dup_string (value);
			break;
		case PROP_NAME:
			self->name = g_value_dup_string (value);
			gtk_label_set_label (GTK_LABEL (self->namew), self->name);
			break;
		case PROP_STATUS:
			self->status = g_value_get_int (value);
			if (self->status) {
				if (self->statusw) {
					gtk_box_remove (GTK_BOX (self->box), self->statusw);
					self->statusw = gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/status_online.svg");
					g_object_set (self->statusw, "halign", GTK_ALIGN_END, "hexpand", TRUE, NULL);
					gtk_widget_set_name (self->statusw, "item");
				}
				gtk_box_append (GTK_BOX (self->box), self->statusw);
			} else {
				if (self->statusw) {
					gtk_box_remove (GTK_BOX (self->box), self->statusw);
					self->statusw = gtk_image_new_from_resource ("/io/github/xverizex/nem_desktop/status_offline.svg");
					g_object_set (self->statusw, "halign", GTK_ALIGN_END, "hexpand", TRUE, NULL);
					gtk_widget_set_name (self->statusw, "item");
				}
				gtk_box_append (GTK_BOX (self->box), self->statusw);
			}
			break;
		case PROP_BLINK:
			self->blink = g_value_get_int (value);
			if (self->blink) gtk_widget_set_name (GTK_WIDGET (self), "blink");
			else gtk_widget_set_name (GTK_WIDGET (self), "noblink");
			break;
		case PROP_BLINK_HANDSHAKE:
			self->blink_handshake = g_value_get_int (value);
			if (self->blink_handshake) gtk_widget_set_name (GTK_WIDGET (self), "blink_handshake");
			else gtk_widget_set_name (GTK_WIDGET (self), "noblink_handshake");
			break;
		case PROP_HANDSHAKED:
			self->handshaked = g_value_get_boolean (value);
			break;
		case PROP_HANDBUTTON:
			self->handbutton = g_value_get_object (value);
			break;
		case PROP_FRAME_CHAT:
			self->frame_chat = g_value_get_object (value);
			break;
		case PROP_OGIO:
			self->ogio = g_value_get_object (value);
			break;
		case PROP_NOTIFICATION:
			self->notification = g_value_get_object (value);
			break;
		case PROP_APP:
			self->app = g_value_get_object (value);
			break;
		case PROP_MAIN_WINDOW:
			self->main_window = g_value_get_object (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void user_item_get_property (GObject *object,
		guint property_id,
		GValue *value,
		GParamSpec *pspec) {
	UserItem *self = USER_ITEM (object);

	switch ((UserItemProperty) property_id) {
		case PROP_ICON:
			g_value_set_string (value, self->icon);
			break;
		case PROP_NAME:
			g_value_set_string (value, self->name);
			break;
		case PROP_BLINK_HANDSHAKE:
			g_value_set_int (value, self->blink_handshake);
			break;
		case PROP_STATUS:
			g_value_set_int (value, self->status);
			break;
		case PROP_BLINK:
			g_value_set_int (value, self->blink);
			break;
		case PROP_HANDSHAKED:
			g_value_set_boolean (value, self->handshaked);
			break;
		case PROP_FRAME_CHAT:
			g_value_set_object (value, self->frame_chat);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}


static void user_item_class_init (UserItemClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = user_item_set_property;
	object_class->get_property = user_item_get_property;

	obj_properties[PROP_ICON] = g_param_spec_string (
			"icon",
			"Icon",
			"icon data in a json format",
			NULL,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_NAME] = g_param_spec_string (
			"name",
			"Name",
			"name of user",
			NULL,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_STATUS] = g_param_spec_int (
			"status",
			"Status",
			"status online",
			0,
			1,
			0,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_BLINK] = g_param_spec_int (
			"blink",
			"Blink",
			"blink item",
			0,
			1,
			0,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_BLINK_HANDSHAKE] = g_param_spec_int (
			"blink_handshake",
			"Blink",
			"blink item",
			0,
			1,
			0,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_HANDSHAKED] = g_param_spec_boolean (
			"handshaking",
			"Handshaking",
			"handshaking",
			FALSE,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_HANDBUTTON] = g_param_spec_object (
			"handbutton",
			"Handbutton",
			"handbutton",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);

	obj_properties[PROP_FRAME_CHAT] = g_param_spec_object (
			"frame_chat",
			"Frame chat",
			"frame_chat",
			G_TYPE_OBJECT,
			G_PARAM_READWRITE
			);

	obj_properties[PROP_OGIO] = g_param_spec_object (
			"ogio",
			"ogio",
			"ogio",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_NOTIFICATION] = g_param_spec_object (
			"notification",
			"notification",
			"notification",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_APP] = g_param_spec_object (
			"app",
			"app",
			"app",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_MAIN_WINDOW] = g_param_spec_object (
			"main_window",
			"main window",
			"main window",
			G_TYPE_OBJECT,
			G_PARAM_WRITABLE
			);

	g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}


void user_item_set_icon (UserItem *item, const char *data) {
	g_object_set (item, "icon", data, NULL);
}

void user_item_set_name (UserItem *item, const char *name) {
	g_object_set (item, "name", name, NULL);
}

void user_item_set_status (UserItem *item, const int status) {
	g_object_set (item, "status", status, NULL);
}

void user_item_set_blink (UserItem *item, const int blink) {
	g_object_set (item, "blink", blink, NULL);
}

const char *user_item_get_icon (UserItem *item) {
	const char *icon;
	g_object_get (item, "icon", &icon, NULL);
	return icon;
}

const char *user_item_get_name (UserItem *item) {
	const char *name;
	g_object_get (item, "name", &name, NULL);
	return name;
}

int user_item_get_status (UserItem *item) {
	int status;
	g_object_get (item, "status", &status, NULL);
	return status;
}

int user_item_get_blink (UserItem *item) {
	int blink;
	g_object_get (item, "blink", &blink, NULL);
	return blink;
}

GtkWidget *user_item_new () {
	return g_object_new (USER_TYPE_ITEM, NULL);
}
