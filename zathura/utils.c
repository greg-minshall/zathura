/* SPDX-License-Identifier: Zlib */

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <gtk/gtk.h>
#include <girara/datastructures.h>
#include <girara/session.h>
#include <girara/settings.h>
#include <girara/utils.h>

#include "links.h"
#include "utils.h"
#include "zathura.h"
#include "internal.h"
#include "document.h"
#include "page.h"
#include "plugin.h"
#include "content-type.h"

double
zathura_correct_zoom_value(girara_session_t* session, const double zoom)
{
  if (session == NULL) {
    return zoom;
  }

  /* zoom limitations */
  int zoom_min_int = 10;
  int zoom_max_int = 1000;
  girara_setting_get(session, "zoom-min", &zoom_min_int);
  girara_setting_get(session, "zoom-max", &zoom_max_int);

  const double zoom_min = zoom_min_int * 0.01;
  const double zoom_max = zoom_max_int * 0.01;

  if (zoom < zoom_min) {
    return zoom_min;
  } else if (zoom > zoom_max) {
    return zoom_max;
  } else {
    return zoom;
  }
}

bool
file_valid_extension(zathura_t* zathura, const char* path)
{
  if (zathura == NULL || path == NULL || zathura->plugins.manager == NULL) {
    return false;
  }

  char* content_type = zathura_content_type_guess(zathura->content_type_context, path);
  if (content_type == NULL) {
    return false;
  }

  zathura_plugin_t* plugin = zathura_plugin_manager_get_plugin(zathura->plugins.manager, content_type);
  g_free(content_type);

  return (plugin == NULL) ? false : true;
}

static void
index_element_free(void* data, GObject* UNUSED(object))
{
  zathura_index_element_t* element = data;
  zathura_index_element_free(element);
}

void
document_index_build(GtkTreeModel* model, GtkTreeIter* parent,
                     girara_tree_node_t* tree)
{
  girara_list_t* list = girara_node_get_children(tree);

  GIRARA_LIST_FOREACH_BODY(list, girara_tree_node_t*, node,
    zathura_index_element_t* index_element = girara_node_get_data(node);

    zathura_link_type_t type     = zathura_link_get_type(index_element->link);
    zathura_link_target_t target = zathura_link_get_target(index_element->link);

    gchar* description = NULL;
    if (type == ZATHURA_LINK_GOTO_DEST) {
      description = g_strdup_printf("Page %d", target.page_number + 1);
    } else {
      description = g_strdup(target.value);
    }

    GtkTreeIter tree_iter;
    gtk_tree_store_append(GTK_TREE_STORE(model), &tree_iter, parent);
    gchar* markup = g_markup_escape_text(index_element->title, -1);
    gtk_tree_store_set(GTK_TREE_STORE(model), &tree_iter, 0, markup, 1, description, 2, index_element, -1);
    g_free(markup);
    g_object_weak_ref(G_OBJECT(model), index_element_free, index_element);
    g_free(description);

    if (girara_node_get_num_children(node) > 0) {
      document_index_build(model, &tree_iter, node);
    }
  );
}

zathura_rectangle_t
rotate_rectangle(zathura_rectangle_t rectangle, unsigned int degree, double height, double width)
{
  zathura_rectangle_t tmp;
  switch (degree) {
    case 90:
      tmp.x1 = height - rectangle.y2;
      tmp.x2 = height - rectangle.y1;
      tmp.y1 = rectangle.x1;
      tmp.y2 = rectangle.x2;
      break;
    case 180:
      tmp.x1 = width - rectangle.x2;
      tmp.x2 = width - rectangle.x1;
      tmp.y1 = height - rectangle.y2;
      tmp.y2 = height - rectangle.y1;
      break;
    case 270:
      tmp.x1 = rectangle.y1;
      tmp.x2 = rectangle.y2;
      tmp.y1 = width - rectangle.x2;
      tmp.y2 = width - rectangle.x1;
      break;
    default:
      tmp.x1 = rectangle.x1;
      tmp.x2 = rectangle.x2;
      tmp.y1 = rectangle.y1;
      tmp.y2 = rectangle.y2;
  }

  return tmp;
}

zathura_rectangle_t
recalc_rectangle(zathura_page_t* page, zathura_rectangle_t rectangle)
{
  if (page == NULL) {
    goto error_ret;
  }

  zathura_document_t* document = zathura_page_get_document(page);

  if (document == NULL) {
    goto error_ret;
  }

  double page_height = zathura_page_get_height(page);
  double page_width  = zathura_page_get_width(page);
  double scale       = zathura_document_get_scale(document);

  zathura_rectangle_t tmp = rotate_rectangle(rectangle, zathura_document_get_rotation(document), page_height, page_width);
  tmp.x1 *= scale;
  tmp.x2 *= scale;
  tmp.y1 *= scale;
  tmp.y2 *= scale;

  return tmp;

error_ret:

  return rectangle;
}

GtkWidget*
zathura_page_get_widget(zathura_t* zathura, zathura_page_t* page)
{
  if (zathura == NULL || page == NULL || zathura->pages == NULL) {
    return NULL;
  }

  unsigned int page_number = zathura_page_get_index(page);

  return zathura->pages[page_number];
}

void
document_draw_search_results(zathura_t* zathura, bool value)
{
  if (zathura == NULL || zathura->document == NULL || zathura->pages == NULL) {
    return;
  }

  unsigned int number_of_pages = zathura_document_get_number_of_pages(zathura->document);
  for (unsigned int page_id = 0; page_id < number_of_pages; page_id++) {
    g_object_set(zathura->pages[page_id], "draw-search-results", (value == true) ? TRUE : FALSE, NULL);
  }
}

char*
zathura_get_version_string(zathura_t* zathura, bool markup)
{
  if (zathura == NULL) {
    return NULL;
  }

  GString* string = g_string_new(NULL);

  /* zathura version */
  g_string_append(string, "zathura " ZATHURA_VERSION);
  g_string_append_printf(string, "\ngirara " GIRARA_VERSION " (runtime: %s)", girara_version());

  const char* format = (markup == true) ? "\n<i>(plugin)</i> %s (%d.%d.%d) <i>(%s)</i>" : "\n(plugin) %s (%d.%d.%d) (%s)";

  /* plugin information */
  girara_list_t* plugins = zathura_plugin_manager_get_plugins(zathura->plugins.manager);
  if (plugins != NULL) {
    GIRARA_LIST_FOREACH_BODY(plugins, zathura_plugin_t*, plugin,
      const char* name = zathura_plugin_get_name(plugin);
      zathura_plugin_version_t version = zathura_plugin_get_version(plugin);
      g_string_append_printf(string, format,
                             (name == NULL) ? "-" : name,
                             version.major,
                             version.minor,
                             version.rev,
                             zathura_plugin_get_path(plugin));
    );
  }

  char* version = string->str;
  g_string_free(string, FALSE);

  return version;
}

GdkAtom*
get_selection(zathura_t* zathura)
{
  g_return_val_if_fail(zathura != NULL, NULL);

  char* value = NULL;
  girara_setting_get(zathura->ui.session, "selection-clipboard", &value);
  if (value == NULL) {
    return NULL;
  }

  GdkAtom* selection = g_try_malloc(sizeof(GdkAtom));
  if (selection == NULL) {
    g_free(selection);
    return NULL;
  }

  if (strcmp(value, "primary") == 0) {
    *selection = GDK_SELECTION_PRIMARY;
  } else if (strcmp(value, "clipboard") == 0) {
    *selection = GDK_SELECTION_CLIPBOARD;
  } else {
    girara_error("Invalid value for the selection-clipboard setting");
    g_free(value);
    g_free(selection);

    return NULL;
  }

  g_free(value);

  return selection;
}

unsigned int
find_first_page_column(const char* first_page_column_list,
                       const unsigned int pages_per_row)
{
  /* sanity checks */
  unsigned int first_page_column = 1;
  g_return_val_if_fail(first_page_column_list != NULL,  first_page_column);
  g_return_val_if_fail(*first_page_column_list != '\0', first_page_column);
  g_return_val_if_fail(pages_per_row > 0,               first_page_column);

  /* split settings list */
  char** settings = g_strsplit(first_page_column_list, ":", pages_per_row + 1);
  const size_t settings_size = g_strv_length(settings);

  /* read setting value corresponding to the specified pages per row */
  unsigned int index = pages_per_row - 1;
  if (index < settings_size && *settings[index] != '\0') {
    first_page_column = atoi(settings[index]);
  } else if (*settings[settings_size - 1] != '\0') {
    first_page_column = atoi(settings[settings_size - 1]);
  }

  /* free buffers */
  g_strfreev(settings);

  return first_page_column;
}

bool
parse_color(GdkRGBA* color, const char* str)
{
  if (gdk_rgba_parse(color, str) == FALSE) {
    girara_warning("Failed to parse color string '%s'.", str);
    return false;
  }
  return true;
}

bool
running_under_wsl(void)
{
  bool result = false;
  char* content = girara_file_read("/proc/version");
  if (content != NULL && g_strstr_len(content, -1, "Microsoft")) {
    result = true;
  }
  free(content);
  return result;
}
