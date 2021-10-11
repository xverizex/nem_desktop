#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define FILE_TYPE_STORAGE              file_storage_get_type ()
G_DECLARE_FINAL_TYPE (FileStorage, file_storage, FILE, STORAGE, GtkFrame)

G_END_DECLS
