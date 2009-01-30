/*
 *  Copyright (c) 2008-2009 Mike Massonnet <mmassonnet@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4panel/xfce-panel-convenience.h>
#include <xfconf/xfconf.h>
#include <glade/glade.h>

#include "common.h"
#include "settings-dialog_glade.h"
#include "actions.h"
#include "collector.h"
#include "history.h"
#include "menu.h"



/*
 * MyPlugin structure
 */

typedef struct _MyPlugin MyPlugin;
struct _MyPlugin
{
  XfcePanelPlugin      *panel_plugin;
  XfconfChannel        *channel;
  ClipmanActions       *actions;
  ClipmanCollector     *collector;
  ClipmanHistory       *history;
  GladeXML             *gxml;
  GtkWidget            *button;
  GtkWidget            *image;
  GtkWidget            *menu;
};

/*
 * Panel Plugin registration
 */

static void panel_plugin_register (XfcePanelPlugin *panel_plugin);
XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL (panel_plugin_register);

/*
 * Panel Plugin functions declarations
 */

static gboolean         panel_plugin_set_size           (XfcePanelPlugin *panel_plugin,
                                                         int size,
                                                         MyPlugin *plugin);
static void             panel_plugin_configure          (XfcePanelPlugin *panel_plugin,
                                                         MyPlugin *plugin);
static void             panel_plugin_load               (XfcePanelPlugin *panel_plugin,
                                                         MyPlugin *plugin);
static void             panel_plugin_save               (XfcePanelPlugin *panel_plugin,
                                                         MyPlugin *plugin);
static void             panel_plugin_free               (XfcePanelPlugin *panel_plugin,
                                                         MyPlugin *plugin);
static void             cb_button_toggled               (GtkToggleButton *button,
                                                         MyPlugin *plugin);
static void             cb_menu_deactivate              (GtkMenuShell *menu,
                                                         MyPlugin *plugin);
static void             my_plugin_position_menu         (GtkMenu *menu,
                                                         gint *x,
                                                         gint *y,
                                                         gboolean *push_in,
                                                         MyPlugin *plugin);

/*
 * Settings Dialog functions declarations
 */

static void             setup_actions_treeview          (GtkTreeView *treeview,
                                                         MyPlugin *plugin);
static void             refresh_actions_treeview        (GtkTreeView *treeview,
                                                         MyPlugin *plugin);
static void             apply_action                    (const gchar *original_action_name,
                                                         MyPlugin *plugin);
static void             cb_actions_selection_changed    (GtkTreeSelection *selection,
                                                         MyPlugin *plugin);
static void             cb_add_action                   (GtkButton *button,
                                                         MyPlugin *plugin);
static void             cb_edit_action                  (GtkButton *button,
                                                         MyPlugin *plugin);
static void             cb_actions_row_activated        (GtkTreeView *treeview,
                                                         GtkTreePath *path,
                                                         GtkTreeViewColumn *column,
                                                         MyPlugin *plugin);
static void             cb_delete_action                (GtkButton *button,
                                                         MyPlugin *plugin);
static void             setup_commands_treeview         (GtkTreeView *treeview,
                                                         MyPlugin *plugin);
static void             entry_dialog_cleanup            (GtkDialog *dialog,
                                                         MyPlugin *plugin);
static void             cb_commands_selection_changed   (GtkTreeSelection *selection,
                                                         MyPlugin *plugin);
static void             cb_add_command                  (GtkButton *button,
                                                         MyPlugin *plugin);
static void             cb_delete_command               (GtkButton *button,
                                                         MyPlugin *plugin);



/*
 * Panel Plugin functions
 */

static void
panel_plugin_register (XfcePanelPlugin *panel_plugin)
{
  MyPlugin *plugin;

  xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");
  xfconf_init (NULL);

  /* XfcePanelPlugin widget */
#if GTK_CHECK_VERSION (2,12,0)
  gtk_widget_set_tooltip_text (GTK_WIDGET (panel_plugin), _("Clipman"));
#endif

  /* MyPlugin */
  plugin = g_slice_new0 (MyPlugin);

  /* Keep a pointer on the panel plugin */
  plugin->panel_plugin = panel_plugin;

  /* XfconfChannel */
  plugin->channel = xfconf_channel_new_with_property_base ("xfce4-panel", "/plugins/clipman");

  /* ClipmanActions */
  plugin->actions = clipman_actions_get ();

  /* ClipmanHistory */
  plugin->history = clipman_history_get ();
  xfconf_g_property_bind (plugin->channel, "/settings/max-texts-in-history",
                          G_TYPE_UINT, plugin->history, "max-texts-in-history");
  xfconf_g_property_bind (plugin->channel, "/settings/max-images-in-history",
                          G_TYPE_UINT, plugin->history, "max-images-in-history");
  xfconf_g_property_bind (plugin->channel, "/settings/save-on-quit",
                          G_TYPE_BOOLEAN, plugin->history, "save-on-quit");

  /* ClipmanCollector */
  plugin->collector = clipman_collector_get ();
  xfconf_g_property_bind (plugin->channel, "/settings/add-primary-clipboard",
                          G_TYPE_BOOLEAN, plugin->collector, "add-primary-clipboard");
  xfconf_g_property_bind (plugin->channel, "/settings/enable-actions",
                          G_TYPE_BOOLEAN, plugin->collector, "enable-actions");

  /* Panel Button */
  plugin->button = xfce_create_panel_toggle_button ();
  /* The image is set through the set_size callback */
  plugin->image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (plugin->button), plugin->image);
  gtk_container_add (GTK_CONTAINER (panel_plugin), plugin->button);
  xfce_panel_plugin_add_action_widget (panel_plugin, plugin->button);
  g_signal_connect (plugin->button, "toggled",
                    G_CALLBACK (cb_button_toggled), plugin);

  /* ClipmanMenu */
  plugin->menu = clipman_menu_new ();
  g_signal_connect (plugin->menu, "deactivate",
                    G_CALLBACK (cb_menu_deactivate), plugin);

  /* Panel Plugin Signals */
  g_signal_connect (panel_plugin, "size-changed",
                    G_CALLBACK (panel_plugin_set_size), plugin);
  xfce_panel_plugin_menu_show_configure (panel_plugin);
  g_signal_connect (panel_plugin, "configure-plugin",
                    G_CALLBACK (panel_plugin_configure), plugin);
  g_signal_connect (panel_plugin, "save",
                    G_CALLBACK (panel_plugin_save), plugin);
  g_signal_connect (panel_plugin, "free-data",
                    G_CALLBACK (panel_plugin_free), plugin);

  /* Load the data */
  panel_plugin_load (panel_plugin, plugin);

  gtk_widget_show_all (GTK_WIDGET (panel_plugin));
}

static gboolean
panel_plugin_set_size (XfcePanelPlugin *panel_plugin,
                       int size,
                       MyPlugin *plugin)
{
  GdkPixbuf *pixbuf;

  gtk_widget_set_size_request (plugin->button, size, size);

  size -= 2 + 2 * MAX (plugin->button->style->xthickness,
                       plugin->button->style->ythickness);
  pixbuf = xfce_themed_icon_load (GTK_STOCK_PASTE, size);
  gtk_image_set_from_pixbuf (GTK_IMAGE (plugin->image), pixbuf);
  g_object_unref (G_OBJECT (pixbuf));

  return TRUE;
}

static void
panel_plugin_configure (XfcePanelPlugin *panel_plugin,
                        MyPlugin *plugin)
{
  GtkWidget *dialog;

  /* GladeXML */
  plugin->gxml = glade_xml_new_from_buffer (settings_dialog_glade, settings_dialog_glade_length, NULL, NULL);

  /* Settings dialog */
  dialog = glade_xml_get_widget (plugin->gxml, "settings-dialog");
  /* NOTE Normally the dialog should be set transient for the top level widget
   * of the panel plugin, but it renders weird bugs doing so with either glade
   * or glade+xfce_titled_dialog. */
  /*
  gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (panel_plugin))));
  */

  /* General settings */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (plugin->gxml, "save-on-quit")),
                                DEFAULT_SAVE_ON_QUIT);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (plugin->gxml, "add-selections")),
                                DEFAULT_ADD_PRIMARY_CLIPBOARD);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (plugin->gxml, "store-an-image")),
                                (gboolean)DEFAULT_MAX_IMAGES_IN_HISTORY);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (glade_xml_get_widget (plugin->gxml, "max-texts-in-history")),
                             (gdouble)DEFAULT_MAX_TEXTS_IN_HISTORY);

  xfconf_g_property_bind (plugin->channel, "/settings/save-on-quit", G_TYPE_BOOLEAN,
                          G_OBJECT (glade_xml_get_widget (plugin->gxml, "save-on-quit")), "active");
  xfconf_g_property_bind (plugin->channel, "/settings/add-primary-clipboard", G_TYPE_BOOLEAN,
                          G_OBJECT (glade_xml_get_widget (plugin->gxml, "add-selections")), "active");
  xfconf_g_property_bind (plugin->channel, "/settings/max-images-in-history", G_TYPE_UINT,
                          G_OBJECT (glade_xml_get_widget (plugin->gxml, "store-an-image")), "active");
  xfconf_g_property_bind (plugin->channel, "/settings/max-texts-in-history", G_TYPE_UINT,
                          G_OBJECT (glade_xml_get_widget (plugin->gxml, "max-texts-in-history")), "value");

  /* Actions */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (plugin->gxml, "enable-actions")),
                                DEFAULT_ENABLE_ACTIONS);
  xfconf_g_property_bind (plugin->channel, "/settings/enable-actions", G_TYPE_BOOLEAN,
                          G_OBJECT (glade_xml_get_widget (plugin->gxml, "enable-actions")), "active");

  glade_xml_signal_connect_data (plugin->gxml, "cb_add_action", G_CALLBACK (cb_add_action), plugin);
  glade_xml_signal_connect_data (plugin->gxml, "cb_edit_action", G_CALLBACK (cb_edit_action), plugin);
  glade_xml_signal_connect_data (plugin->gxml, "cb_delete_action", G_CALLBACK (cb_delete_action), plugin);
  glade_xml_signal_connect_data (plugin->gxml, "cb_actions_row_activated", G_CALLBACK (cb_actions_row_activated), plugin);
  glade_xml_signal_connect_data (plugin->gxml, "cb_add_command", G_CALLBACK (cb_add_command), plugin);
  glade_xml_signal_connect_data (plugin->gxml, "cb_delete_command", G_CALLBACK (cb_delete_command), plugin);

  setup_actions_treeview (GTK_TREE_VIEW (glade_xml_get_widget (plugin->gxml, "actions")), plugin);
  setup_commands_treeview (GTK_TREE_VIEW (glade_xml_get_widget (plugin->gxml, "commands")), plugin);

  /* Run the dialog */
  xfce_panel_plugin_block_menu (panel_plugin);
  gtk_dialog_run (GTK_DIALOG (dialog));
  xfce_panel_plugin_unblock_menu (panel_plugin);

  gtk_widget_destroy (dialog);
  g_object_unref (plugin->gxml);
  plugin->gxml = NULL;

  /* Save the actions */
  clipman_actions_save (plugin->actions);
}

static void
panel_plugin_load (XfcePanelPlugin *panel_plugin,
                   MyPlugin *plugin)
{
  GtkClipboard *clipboard;
  GKeyFile *keyfile;
  gchar **texts = NULL;
  gchar *filename;
  GdkPixbuf *image;
  gint i = 0;
  gboolean save_on_quit;

  /* Return if the history must not be saved */
  g_object_get (plugin->history, "save-on-quit", &save_on_quit, NULL);
  if (save_on_quit == FALSE)
    return;

  clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  /* Load images */
  while (TRUE)
    {
      filename = g_strdup_printf ("%s/xfce4/clipman/image%d.png", g_get_user_cache_dir (), i++);
      DBG ("Loading image from cache file %s", filename);
      image = gdk_pixbuf_new_from_file (filename, NULL);
      g_unlink (filename);
      g_free (filename);
      if (image == NULL)
        break;

      clipman_history_add_image (plugin->history, image);
      g_object_unref (image);
    }

  /* Load texts */
  filename = g_strdup_printf ("%s/xfce4/clipman/textsrc", g_get_user_cache_dir ());
  DBG ("Loading texts from cache file %s", filename);
  keyfile = g_key_file_new ();
  if (g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, NULL))
    {
      texts = g_key_file_get_string_list (keyfile, "texts", "texts", NULL, NULL);
      for (i = 0; texts != NULL && texts[i] != NULL; i++)
        clipman_history_add_text (plugin->history, texts[i]);
      g_unlink (filename);
    }

  g_key_file_free (keyfile);
  g_strfreev (texts);
  g_free (filename);

  /* Set no current item */
  clipman_history_set_item_to_restore (plugin->history, NULL);
}

static void
panel_plugin_save (XfcePanelPlugin *panel_plugin,
                   MyPlugin *plugin)
{
  GSList *list, *l;
  const ClipmanHistoryItem *item;
  GKeyFile *keyfile;
  const gchar **texts;
  gchar *data;
  gchar *filename;
  gint n_texts, n_images;
  gboolean save_on_quit;

  /* Return if the history must not be saved */
  g_object_get (plugin->history, "save-on-quit", &save_on_quit, NULL);
  if (save_on_quit == FALSE)
    return;

  /* Create initial directory */
  filename = xfce_resource_save_location (XFCE_RESOURCE_CACHE, "xfce4/clipman/", TRUE);
  g_free (filename);

  /* Save the history */
  list = clipman_history_get_list (plugin->history);
  if (list != NULL)
    {
      texts = g_malloc0 (g_slist_length (list) * sizeof (gchar *));
      for (n_texts = n_images = 0, l = list; l != NULL; l = l->next)
        {
          item = l->data;

          switch (item->type)
            {
            case CLIPMAN_HISTORY_TYPE_TEXT:
              texts[n_texts++] = item->content.text;
              break;

            case CLIPMAN_HISTORY_TYPE_IMAGE:
              filename = g_strdup_printf ("%s/xfce4/clipman/image%d.png", g_get_user_cache_dir (), n_images++);
              if (!gdk_pixbuf_save (item->content.image, filename, "png", NULL, NULL))
                g_warning ("Failed to save image to cache file %s", filename);
              else
                DBG ("Saved image to cache file %s", filename);
              g_free (filename);
              break;

            default:
              g_assert_not_reached ();
            }
        }

      if (n_texts > 0)
        {
          filename = g_strdup_printf ("%s/xfce4/clipman/textsrc", g_get_user_cache_dir ());
          keyfile = g_key_file_new ();
          g_key_file_set_string_list (keyfile, "texts", "texts", texts, n_texts);
          data = g_key_file_to_data (keyfile, NULL, NULL);
          g_file_set_contents (filename, data, -1, NULL);
          DBG ("Saved texts to cache file %s", filename);

          g_key_file_free (keyfile);
          g_free (data);
          g_free (filename);
          g_free (texts);
        }

      g_slist_free (list);
    }
}

static void
panel_plugin_free (XfcePanelPlugin *panel_plugin,
                   MyPlugin *plugin)
{
  gtk_widget_destroy (plugin->menu);
  gtk_widget_destroy (plugin->button);
  g_object_unref (plugin->channel);
  g_object_unref (plugin->actions);
  g_object_unref (plugin->collector);
  g_object_unref (plugin->history);
  g_slice_free (MyPlugin, plugin);
  xfconf_shutdown ();
}

static void
cb_button_toggled (GtkToggleButton *button,
                   MyPlugin *plugin)
{
  if (gtk_toggle_button_get_active (button))
    {
      gtk_menu_set_screen (GTK_MENU (plugin->menu), gtk_widget_get_screen (plugin->button));
      xfce_panel_plugin_register_menu (plugin->panel_plugin, GTK_MENU (plugin->menu));
      gtk_menu_popup (GTK_MENU (plugin->menu), NULL, NULL,
                      (GtkMenuPositionFunc)my_plugin_position_menu, plugin,
                      0, gtk_get_current_event_time ());
    }
}

static void
cb_menu_deactivate (GtkMenuShell *menu,
                    MyPlugin *plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), FALSE);
}

static void
my_plugin_position_menu (GtkMenu *menu,
                         gint *x,
                         gint *y,
                         gboolean *push_in,
                         MyPlugin *plugin)
{
  GtkWidget *button;
  GtkRequisition requisition;
  GtkOrientation orientation;

  button = plugin->button;
  orientation = xfce_panel_plugin_get_orientation (plugin->panel_plugin);
  gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
  gdk_window_get_origin (button->window, x, y);

  switch (orientation)
    {
    case GTK_ORIENTATION_HORIZONTAL:
      if (*y + button->allocation.height + requisition.height > gdk_screen_height ())
        /* Show menu above */
        *y -= requisition.height;
      else
        /* Show menu below */
        *y += button->allocation.height;

      if (*x + requisition.width > gdk_screen_width ())
        /* Adjust horizontal position */
        *x = gdk_screen_width () - requisition.width;
      break;

    case GTK_ORIENTATION_VERTICAL:
      if (*x + button->allocation.width + requisition.width > gdk_screen_width ())
        /* Show menu on the right */
        *x -= requisition.width;
      else
        /* Show menu on the left */
        *x += button->allocation.width;

      if (*y + requisition.height > gdk_screen_height ())
        /* Adjust vertical position */
        *y = gdk_screen_height () - requisition.height;
      break;

    default:
      break;
    }
}

/*
 * Settings Dialog functions
 */

/* Actions */
static void
setup_actions_treeview (GtkTreeView *treeview,
                        MyPlugin *plugin)
{
  GtkTreeSelection *selection;
  GtkListStore *model;
  GtkCellRenderer *cell;

  /* Define the model */
  model = gtk_list_store_new (3, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_STRING);
  gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (model));
  g_object_unref (model);

  /* Define the columns */
  cell = gtk_cell_renderer_pixbuf_new ();
  /* TODO Drop the comment once the icon is supported */
  //g_object_set (cell, "width", 32, "height", 32, NULL);
  gtk_tree_view_insert_column_with_attributes (treeview, -1, "Icon", cell, "icon-name", 1, NULL);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_insert_column_with_attributes (treeview, -1, "Action", cell, "markup", 2, NULL);

  refresh_actions_treeview (treeview, plugin);

  selection = gtk_tree_view_get_selection (treeview);
  g_signal_connect (selection, "changed", G_CALLBACK (cb_actions_selection_changed), plugin);
}

static void
refresh_actions_treeview (GtkTreeView *treeview,
                          MyPlugin *plugin)
{
  ClipmanActionsEntry *entry;
  const GSList *entries;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *title;

  model = gtk_tree_view_get_model (treeview);
  gtk_list_store_clear (GTK_LIST_STORE (model));

  entries = clipman_actions_get_entries (plugin->actions);
  for (; entries != NULL; entries = entries->next)
    {
      entry = entries->data;

      title = g_strdup_printf ("<b>%s</b>\n<small>%s</small>", entry->action_name, g_regex_get_pattern (entry->regex));
      gtk_list_store_append (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, entry, 1, entry->icon_name, 2, title, -1);
      g_free (title);
    }
}

static void
apply_action (const gchar *original_action_name,
              MyPlugin *plugin)
{
  GtkWidget *treeview;
  GtkTreeModel *model;
  GtkTreeIter iter;
  const gchar *action_name;
  const gchar *regex;
  gchar *command_name;
  gchar *command;

  action_name = gtk_entry_get_text (GTK_ENTRY (glade_xml_get_widget (plugin->gxml, "action-name")));
  regex = gtk_entry_get_text (GTK_ENTRY (glade_xml_get_widget (plugin->gxml, "regex")));

  treeview = glade_xml_get_widget (plugin->gxml, "commands");
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
  if (!gtk_tree_model_get_iter_first (model, &iter))
    return;

  /* Remove the old actions */
  if (original_action_name != NULL)
    clipman_actions_remove (plugin->actions, original_action_name);

  /* Add the new actions */
  do
    {
      gtk_tree_model_get (model, &iter, 1, &command_name, 2, &command, -1);
      clipman_actions_add (plugin->actions, action_name, regex, command_name, command);
      g_free (command_name);
      g_free (command);
    }
  while (gtk_tree_model_iter_next (model, &iter));

  /* Refresh the actions treeview */
  treeview = glade_xml_get_widget (plugin->gxml, "actions");
  refresh_actions_treeview (GTK_TREE_VIEW (treeview), plugin);
}

static void
cb_actions_selection_changed (GtkTreeSelection *selection,
                              MyPlugin *plugin)
{
  GtkTreeModel *model;
  gboolean sensitive;

  sensitive = gtk_tree_selection_get_selected (selection, &model, NULL);

  gtk_widget_set_sensitive (glade_xml_get_widget (plugin->gxml, "button-edit-action"), sensitive);
  gtk_widget_set_sensitive (glade_xml_get_widget (plugin->gxml, "button-delete-action"), sensitive);
}

static void
cb_add_action (GtkButton *button,
               MyPlugin *plugin)
{
  GtkWidget *dialog;
  gint res;

  dialog = glade_xml_get_widget (plugin->gxml, "entry-dialog");
  entry_dialog_cleanup (GTK_DIALOG (dialog), plugin);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);

  if (res == 1)
    apply_action (NULL, plugin);
}

static void
cb_edit_action (GtkButton *button,
                MyPlugin *plugin)
{
  GtkWidget *treeview;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  GtkTreeViewColumn *column;

  treeview = glade_xml_get_widget (plugin->gxml, "actions");

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      g_critical ("Trying to edit an action but got no selection");
      return;
    }

  path = gtk_tree_model_get_path (model, &iter);
  column = gtk_tree_view_get_column (GTK_TREE_VIEW (treeview), 1);
  gtk_tree_view_row_activated (GTK_TREE_VIEW (treeview), path, column);
  gtk_tree_path_free (path);
}

static void
cb_actions_row_activated (GtkTreeView *treeview,
                          GtkTreePath *path,
                          GtkTreeViewColumn *column,
                          MyPlugin *plugin)
{
  ClipmanActionsEntry *entry;
  GtkTreeModel *actions_model, *commands_model;
  GtkTreeIter iter;
  GtkWidget *dialog;
  gchar *title;
  gint res;

  dialog = glade_xml_get_widget (plugin->gxml, "entry-dialog");
  entry_dialog_cleanup (GTK_DIALOG (dialog), plugin);

  actions_model = gtk_tree_view_get_model (treeview);
  gtk_tree_model_get_iter (actions_model, &iter, path);
  gtk_tree_model_get (actions_model, &iter, 0, &entry, -1);

  gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (plugin->gxml, "action-name")), entry->action_name);
  gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (plugin->gxml, "regex")), g_regex_get_pattern (entry->regex));

  commands_model = gtk_tree_view_get_model (GTK_TREE_VIEW (glade_xml_get_widget (plugin->gxml, "commands")));
#if GLIB_CHECK_VERSION (2,16,0)
  GHashTableIter hiter;
  gpointer key, value;
  g_hash_table_iter_init (&hiter, entry->commands);
  while (g_hash_table_iter_next (&hiter, &key, &value))
    {
      title = g_strdup_printf ("<b>%s</b>\n<small>%s</small>", (gchar *)key, (gchar *)value);
      gtk_list_store_append (GTK_LIST_STORE (commands_model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (commands_model), &iter, 0, title, 1, key, 2, value, -1);
      g_free (title);
    }
#endif

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);

  if (res == 1)
    apply_action (entry->action_name, plugin);
}

static void
cb_delete_action (GtkButton *button,
                  MyPlugin *plugin)
{
  ClipmanActionsEntry *entry;
  GtkWidget *treeview;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;

  treeview = glade_xml_get_widget (plugin->gxml, "actions");

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      g_critical ("Trying to remove an action but got no selection");
      return;
    }

  gtk_tree_model_get (model, &iter, 0, &entry, -1);
  clipman_actions_remove (plugin->actions, entry->action_name);
  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

/* Entry Dialog */
static void
setup_commands_treeview (GtkTreeView *treeview,
                         MyPlugin *plugin)
{
  GtkTreeSelection *selection;
  GtkListStore *model;
  GtkCellRenderer *cell;

  /* Define the model */
  model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (model));
  g_object_unref (model);

  /* Define the columns */
  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_insert_column_with_attributes (treeview, -1, "Command", cell, "markup", 0, NULL);

  selection = gtk_tree_view_get_selection (treeview);
  g_signal_connect (selection, "changed", G_CALLBACK (cb_commands_selection_changed), plugin);
}

static void
entry_dialog_cleanup (GtkDialog *dialog,
                      MyPlugin *plugin)
{
  GtkTreeModel *model;

  gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (plugin->gxml, "action-name")), "");
  gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (plugin->gxml, "regex")), "");

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (glade_xml_get_widget (plugin->gxml, "commands")));
  gtk_list_store_clear (GTK_LIST_STORE (model));
}

static void
cb_commands_selection_changed (GtkTreeSelection *selection,
                               MyPlugin *plugin)
{
  GtkTreeModel *model;
  gboolean sensitive;

  sensitive = gtk_tree_selection_get_selected (selection, &model, NULL);

  gtk_widget_set_sensitive (glade_xml_get_widget (plugin->gxml, "button-delete-command"), sensitive);
}

static void
cb_add_command (GtkButton *button,
                MyPlugin *plugin)
{
  GtkWidget *dialog;
  GtkWidget *command_name;
  GtkWidget *command;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *title;
  gint res;

  dialog = glade_xml_get_widget (plugin->gxml, "command-dialog");
  command_name = glade_xml_get_widget (plugin->gxml, "command-name");
  command = glade_xml_get_widget (plugin->gxml, "command");

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);

  /* TODO remove the get_text checks once the sensitivity of ok button is handled */
  if (res == 1 && gtk_entry_get_text (GTK_ENTRY (command_name))[0] != '\0'
      && gtk_entry_get_text (GTK_ENTRY (command))[0] != '\0')
    {
      model = gtk_tree_view_get_model (GTK_TREE_VIEW (glade_xml_get_widget (plugin->gxml, "commands")));
      title = g_strdup_printf ("<b>%s</b>\n<small>%s</small>",
                               gtk_entry_get_text (GTK_ENTRY (command_name)),
                               gtk_entry_get_text (GTK_ENTRY (command)));
      gtk_list_store_append (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, title,
                          1, gtk_entry_get_text (GTK_ENTRY (command_name)),
                          2, gtk_entry_get_text (GTK_ENTRY (command)), -1);
      g_free (title);
    }

  gtk_entry_set_text (GTK_ENTRY (command_name), "");
  gtk_entry_set_text (GTK_ENTRY (command), "");
}

static void
cb_delete_command (GtkButton *button,
                   MyPlugin *plugin)
{
  GtkWidget *treeview;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;

  treeview = glade_xml_get_widget (plugin->gxml, "commands");

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      g_critical ("Trying to delete a command but got no selection");
      return;
    }

  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

