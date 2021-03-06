/* message-item.c
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

#include "message-item.h"
#include "main-window.h"

struct points {
  int x;
  int y;
  int width;
  int height;
  int selected;
  char buf[10];
  int length_buf;
  double red, green, blue;
};

struct _MessageItem {
	GtkDrawingArea parent_instance;

	char *text;
  struct points *p;
	int max_width;
  int id;
  int pressed;
  double point_x;
  double point_y;
  int length_text;
  int index_end;
  int index_start;
  int selected;
  int available_text;
  char buf_time[128];
  GtkWidget *popover;
  MainWindow *main_window;
  GMenu *menu;
  GtkApplication *app;
  int size_text;
  int length_text_in_char;

};


G_DEFINE_TYPE (MessageItem, message_item, GTK_TYPE_DRAWING_AREA)



typedef enum {
	PROP_TEXT = 1,
	PROP_MAX_WIDTH,
  PROP_ID,
  PROP_MAIN_WINDOW,
  PROP_APP,
	N_PROPERTIES
} MessageItemProperties;

#define OFFSET            20

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void draw_function (GtkDrawingArea *area,
		cairo_t *cr,
		int width,
		int height,
		gpointer data)
{
	MessageItem *self = MESSAGE_ITEM (area);



	int total_w = self->max_width;
	int total_h = 0;
	cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_set_font_size (cr, 12);
	cairo_text_extents_t sz;
	cairo_text_extents (cr, self->buf_time, &sz);
	total_h = sz.height + OFFSET + 10;
  int x = self->max_width / 2 - sz.width / 2 - OFFSET;
  cairo_save (cr);
  cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
  cairo_move_to (cr, x - 10, 0);
  cairo_line_to (cr, x, sz.height + 8);
  cairo_line_to (cr, x + sz.width, sz.height + 8);
  cairo_line_to (cr, x + sz.width + 10, 0);
  cairo_line_to (cr, x - 10, 0);
  cairo_move_to (cr, x, total_h);
  cairo_fill (cr);
  cairo_restore (cr);

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_move_to (cr, x, sz.height + 4);
	cairo_show_text (cr, self->buf_time);

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
	cairo_set_font_size (cr, 16);
 	cairo_text_extents (cr, self->text, &sz);

  int max_count = g_utf8_strlen (self->text, -1);

  x = OFFSET;
  int length_text = 0;
  char *s = self->text;
  if (max_count <= 0) return;
  for (int i = 0; i < max_count; i++) {
    int count = 1;
    char buf[16];
    while (!g_utf8_validate (s, count, NULL)) count++;
    strncpy (buf, s, count);
    buf[count] = 0;
    cairo_text_extents (cr, buf, &sz);
    if ((x + sz.x_advance) >= self->max_width - OFFSET) {
      total_h += 16 + 10;
      x = OFFSET;
    }
    cairo_move_to (cr, x, total_h);
    self->p[i].x = x;
    self->p[i].y = total_h;
    self->p[i].width = sz.x_advance;
    self->p[i].height = sz.height;
    strncpy (self->p[i].buf, s, count);
    self->p[i].buf[count] = 0;
    self->p[i].length_buf = count;
    x += sz.x_advance;
    cairo_set_source_rgb (cr, self->p[i].red, self->p[i].green, self->p[i].blue);
    cairo_show_text (cr, buf);
    s += count;
    length_text += count;
  }
  total_h += 16;
  self->length_text_in_char = length_text;

	gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (self), self->max_width);
	gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (self), total_h + OFFSET);
}
static void copy_text_cb (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data);

static void message_item_set_property (GObject *object,
		guint property_id,
		const GValue *value,
		GParamSpec *pspec) {
	MessageItem *self = MESSAGE_ITEM (object);

	switch ((MessageItemProperties) property_id) {
  case PROP_APP:
      {
        self->app = g_value_get_object (value);

        break;
      }
		case PROP_TEXT:
      {
        GDateTime *datet = g_date_time_new_now_local ();
        int hour = g_date_time_get_hour (datet);
        int minute = g_date_time_get_minute (datet);
        int second = g_date_time_get_second (datet);
        g_date_time_unref (datet);
        snprintf (self->buf_time, 255, "%02d:%02d:%02d", hour, minute, second);
      }

			if (self->text) g_free (self->text);
			self->text = g_value_dup_string (value);
      if (self->text == NULL) break;
      self->length_text = g_utf8_strlen (self->text, -1);
      self->p = calloc (self->length_text, sizeof (struct points));
			break;
  case PROP_ID:
    self->id = g_value_get_int (value);
    break;
		case PROP_MAX_WIDTH:
			self->max_width = g_value_get_int (value);
			break;
		default:
  case PROP_MAIN_WINDOW:
      {
        self->main_window = g_value_get_object (value);
      }
      break;
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void button_copy_clicked_cb (GtkButton *button,
                                    gpointer   user_data)
{
  MessageItem *self = MESSAGE_ITEM (user_data);

}

extern MessageItem *message_item;

static void popover_close_cb (GtkPopover *popover,
                              gpointer    user_data)
{
  MessageItem *self = MESSAGE_ITEM (user_data);
  message_item = self;
}

static void size_allocate (GtkWidget *widget,
                           int        width,
                           int        height,
                           int        baseline)
{
  MessageItem *self = MESSAGE_ITEM (widget);

  GMenu *menu = g_menu_new ();
  g_menu_append (menu, "COPY", "app.copy");
  self->popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  g_signal_connect (self->popover, "closed", G_CALLBACK (popover_close_cb), self);
  gtk_widget_set_parent (self->popover, GTK_WIDGET (self));
  gtk_popover_present (self->popover);
}

static void message_item_class_init (MessageItemClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *gtk_class = GTK_WIDGET_CLASS (klass);
  gtk_class->size_allocate = size_allocate;

	object_class->set_property = message_item_set_property;

	obj_properties[PROP_TEXT] = g_param_spec_string (
			"text",
			"text",
			"text",
			NULL,
			G_PARAM_WRITABLE
			);
	obj_properties[PROP_MAX_WIDTH] = g_param_spec_int (
			"max_width",
			"max width",
			"set max width for frame",
			100,
			1080,
			400,
			G_PARAM_WRITABLE
			);
  obj_properties[PROP_ID] = g_param_spec_int (
			"id",
			"id of message",
			"set id of message",
			0,
			9999,
			0,
			G_PARAM_WRITABLE
			);
  obj_properties[PROP_MAIN_WINDOW] = g_param_spec_object (
			"main_window",
			"main_window",
      "main_window",
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

	g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}



static void event_motion_cb (GtkEventControllerMotion *motion,
                             double                    x,
                             double                    y,
                             gpointer                  user_data)
{
  MessageItem *self = MESSAGE_ITEM (user_data);
  if (!self->pressed) return;

  int xx = x;
  int yy = y;

  int i = 0;
  int direction = 0;

  if (y <= self->point_y) {
    i = self->length_text - 1;
    direction = 1;
  } else {
    i = 0;
    direction = 0;
  }

  while (1)
    {
      if (i == -1) break;
        if ((xx >= self->p[i].x && xx <= (self->p[i].x + self->p[i].width)))
            {
              do {
              if (direction) {
                if (yy <= self->p[i].y && yy >= self->p[i].y - self->p[i].height)
                  {
                    self->index_start = i;
                    if (self->index_end > 0) break;
                    self->index_end = i;
                  }

              } else {
                if (yy <= self->p[i].y && yy >= self->p[i].y - self->p[i].height)
                  {
                    self->index_end = i;
                    if (self->index_start > 0) break;
                    self->index_start = i;
                  }

              }
              } while (0);


            }

      if (direction) {
        i--;
        if (i >= 0) continue;
        else break;
      }
      else {
        i++;
        if (i < self->length_text) continue;
        else break;
      }
    }



  if (self->index_start == -1 || self->index_end == -1) return;

  self->selected = 0;
  self->size_text = 0;
  for (int i = 0; i < self->length_text; i++) {
    if (i >= self->index_start && i <= self->index_end) {
      self->p[i].red = 1.0;
      self->p[i].green = 0.0;
      self->p[i].blue = 0.0;
      self->p[i].selected = 1;
      self->selected = 1;
      self->size_text++;
    } else {
      self->p[i].red = 0.0;
      self->p[i].green = 0.0;
      self->p[i].blue = 0.0;
      self->p[i].selected = 0;
    }
  }

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void gesture_pressed_cb (GtkGestureClick *gesture,
                           int              n_press,
                           double           x,
                           double           y,
                           gpointer         user_data)
{
  MessageItem *self = MESSAGE_ITEM (user_data);
  self->point_y = y;
  self->point_x = x;
  self->index_end = -1;
  self->index_start = -1;
  self->pressed = 1;
}


static void gesture_released_cb (GtkGestureClick *gesture,
                           int              n_press,
                           double           x,
                           double           y,
                           gpointer         user_data)
{
  MessageItem *self = MESSAGE_ITEM (user_data);
  self->pressed = 0;

  if (self->selected)
    {
      gtk_popover_popup (GTK_POPOVER (self->popover));
    }

}

void message_item_copy (MessageItem *self)
{
  char *t = malloc (self->length_text_in_char + 1);
  int index = 0;
  for (int i = self->index_start; i <= self->index_end; i++)
    {
      if (index >= self->length_text_in_char) break;
      if (self->p[i].selected) {
        strncpy (&t[index], self->p[i].buf, self->p[i].length_buf);
        index += self->p[i].length_buf;

      }
    }
  t[index] = 0;
  GdkDisplay *display = gdk_display_get_default ();
  GdkClipboard *clipboard = gdk_display_get_clipboard (display);
  gdk_clipboard_set_text (clipboard, t);
  free (t);
}

static void message_item_init (MessageItem *self) {
	gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (self), 10);
	gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (self), 10);
	gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self), draw_function, NULL, NULL);

  GtkGesture *gesture_click = gtk_gesture_click_new ();
  g_signal_connect (gesture_click, "pressed", G_CALLBACK (gesture_pressed_cb), self);
  g_signal_connect (gesture_click, "released", G_CALLBACK (gesture_released_cb), self);

  GtkEventController *event_motion = gtk_event_controller_motion_new ();
  g_signal_connect (event_motion, "motion", G_CALLBACK (event_motion_cb), self);


  gtk_widget_add_controller (GTK_WIDGET (self), event_motion);
  gtk_widget_add_controller (GTK_WIDGET (self), gesture_click);


}
