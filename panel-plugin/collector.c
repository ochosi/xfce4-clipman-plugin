/*
 *  Copyright (c) 2008-2012 Mike Massonnet <mmassonnet@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>

#include "common.h"
#include "actions.h"
#include "history.h"

#include "collector.h"

/*
 * GObject declarations
 */

G_DEFINE_TYPE (ClipmanCollector, clipman_collector, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLIPMAN_TYPE_COLLECTOR, ClipmanCollectorPrivate))

struct _ClipmanCollectorPrivate
{
  ClipmanActions       *actions;
  ClipmanHistory       *history;
  GtkClipboard         *default_clipboard;
  GtkClipboard         *primary_clipboard;
  guint                 primary_clipboard_timeout;
  gboolean              internal_change;
  gboolean              add_primary_clipboard;
  gboolean              history_ignore_primary_clipboard;
  gboolean              enable_actions;
  gboolean              inhibit;
};

enum
{
  ADD_PRIMARY_CLIPBOARD = 1,
  HISTORY_IGNORE_PRIMARY_CLIPBOARD,
  ENABLE_ACTIONS,
  INHIBIT,
};

static void             clipman_collector_constructed       (GObject *object);
static void             clipman_collector_finalize          (GObject *object);
static void             clipman_collector_set_property      (GObject *object,
                                                             guint property_id,
                                                             const GValue *value,
                                                             GParamSpec *pspec);
static void             clipman_collector_get_property      (GObject *object,
                                                             guint property_id,
                                                             GValue *value,
                                                             GParamSpec *pspec);

/*
 * Callbacks declarations
 */

static void             cb_clipboard_owner_change           (ClipmanCollector *collector,
                                                             GdkEventOwnerChange *event);
static gboolean         cb_check_primary_clipboard          (ClipmanCollector *collector);
static void             cb_request_text                     (GtkClipboard *clipboard,
                                                             const gchar *text,
                                                             ClipmanCollector *collector);



/*
 * Callbacks
 */

static void
cb_clipboard_owner_change (ClipmanCollector *collector,
                           GdkEventOwnerChange *event)
{
  GdkPixbuf *image;

  g_return_if_fail (GTK_IS_CLIPBOARD (collector->priv->default_clipboard) && GTK_IS_CLIPBOARD (collector->priv->primary_clipboard));

  /* Jump over if collector is inhibited */
  if (collector->priv->inhibit)
    {
      return;
    }

  /* Jump over if the content is set from within clipman */
  if (collector->priv->internal_change)
    {
      collector->priv->internal_change = FALSE;
      return;
    }

  /* Save the clipboard content to ClipmanHistory */
  if (event->selection == GDK_SELECTION_CLIPBOARD)
    {
      if (gtk_clipboard_wait_is_image_available (collector->priv->default_clipboard))
        {
          image = gtk_clipboard_wait_for_image (collector->priv->default_clipboard);
          if (image != NULL)
            clipman_history_add_image (collector->priv->history, image);
          g_object_unref (image);
        }
      else
        {
          gtk_clipboard_request_text (collector->priv->default_clipboard,
                                      (GtkClipboardTextReceivedFunc)cb_request_text,
                                      collector);
        }
    }
  else if (event->selection == GDK_SELECTION_PRIMARY)
    {
      /* This clipboard is due to many changes while selecting, therefore we
       * actually check inside a delayed timeout if the mouse is still pressed
       * or if the shift key is hold down, and once both are released the
       * content will go to the history. */
      if (collector->priv->add_primary_clipboard
          || !collector->priv->history_ignore_primary_clipboard
          || collector->priv->enable_actions)
        {
          if (collector->priv->primary_clipboard_timeout == 0)
            collector->priv->primary_clipboard_timeout =
              g_timeout_add (250, (GSourceFunc)cb_check_primary_clipboard, collector);
        }
    }
}

static gboolean
cb_check_primary_clipboard (ClipmanCollector *collector)
{
  GdkModifierType state = 0;
  GdkDisplay* display = gdk_display_get_default ();
#if GTK_CHECK_VERSION (3, 20, 0)
  GdkSeat *seat = gdk_display_get_default_seat (display);
  GdkDevice *device = gdk_seat_get_pointer (seat);
#else
  GdkDeviceManager *device_manager = gdk_display_get_device_manager (display);
  GdkDevice *device = gdk_device_manager_get_client_pointer (device_manager);
#endif
  GdkScreen* screen = gdk_screen_get_default ();
  GdkWindow * root_win = gdk_screen_get_root_window (screen);

  g_return_val_if_fail (GTK_IS_CLIPBOARD (collector->priv->default_clipboard) && GTK_IS_CLIPBOARD (collector->priv->primary_clipboard), FALSE);

  /* Postpone until the selection is done */
  gdk_window_get_device_position (root_win, device, NULL, NULL, &state);
  if (state & (GDK_BUTTON1_MASK|GDK_SHIFT_MASK))
    return TRUE;

  gtk_clipboard_request_text (collector->priv->primary_clipboard,
                              (GtkClipboardTextReceivedFunc)cb_request_text,
                              collector);

  collector->priv->primary_clipboard_timeout = 0;
  return FALSE;
}

static void
cb_request_text (GtkClipboard *clipboard,
                 const gchar *text,
                 ClipmanCollector *collector)
{
  static gchar *prev_text = NULL;

  g_return_if_fail (GTK_IS_CLIPBOARD (collector->priv->default_clipboard) && GTK_IS_CLIPBOARD (collector->priv->primary_clipboard));

  if (text == NULL || text[0] == '\0')
    return;

  if (clipboard == collector->priv->default_clipboard)
    {
      clipman_history_add_text (collector->priv->history, text);
      if (collector->priv->enable_actions)
        clipman_actions_match_with_menu (collector->priv->actions, ACTION_GROUP_MANUAL, text);
    }
  else if (clipboard == collector->priv->primary_clipboard)
    {
      /* Avoid history */
      if (collector->priv->add_primary_clipboard && collector->priv->history_ignore_primary_clipboard)
        collector->priv->internal_change = TRUE;
      else if (!collector->priv->history_ignore_primary_clipboard)
        clipman_history_add_text (collector->priv->history, text);

      /* Make a copy inside the default clipboard */
      if (collector->priv->add_primary_clipboard)
        gtk_clipboard_set_text (collector->priv->default_clipboard, text, -1);

      /* Match for actions */
      if (collector->priv->enable_actions && g_strcmp0 (text, prev_text))
        {
          clipman_actions_match_with_menu (collector->priv->actions, ACTION_GROUP_SELECTION, text);
          g_free (prev_text);
          prev_text = g_strdup (text);
        }
    }
}

/*
 * Public methods
 */

/**
 * clipman_collector_set_is_restoring:
 * @collector: a #ClipmanCollector
 *
 * Call this function before modifing the content of a #GtkClipboard so that
 * the new content won't be looked by #ClipmanCollector.  Useful to prevent an
 * image from being saved twice.  Note that for text this is useless because
 * texts are compared, and a match throws the text in the history to the top of
 * the list.
 * See also clipman_history_set_item_to_restore().
 */
void
clipman_collector_set_is_restoring (ClipmanCollector *collector)
{
  collector->priv->internal_change = TRUE;
}

ClipmanCollector *
clipman_collector_get (void)
{
  static ClipmanCollector *singleton = NULL;

  if (singleton == NULL)
    {
      singleton = g_object_new (CLIPMAN_TYPE_COLLECTOR, NULL);
      g_object_add_weak_pointer (G_OBJECT (singleton), (gpointer)&singleton);
    }
  else
    g_object_ref (G_OBJECT (singleton));

  return singleton;
}

void
clipman_collector_show_actions (void)
{
  ClipmanCollector   *collector;
  const ClipmanHistoryItem *item;
  ClipmanHistory     *history;
  GSList *entries;
  gint group;
    
  history = clipman_history_get();
  collector = clipman_collector_get();

  item = clipman_history_get_item_to_restore(history);

  if (item == NULL)
    return;

  if (item->type == CLIPMAN_HISTORY_TYPE_TEXT)
    {
      entries = clipman_actions_match (collector->priv->actions, ACTION_GROUP_MANUAL, item->content.text);
      if (entries == NULL)
        {
          group = ACTION_GROUP_SELECTION;
        }
      else
        {
          group = ACTION_GROUP_MANUAL;
        }
      g_slist_free (entries);
      clipman_actions_match_with_menu (collector->priv->actions, group, item->content.text);
    }
  else
    {
      DBG("I can't show actions on item type %d", item->type);
    }
}

/*
 * GObject
 */

static void
clipman_collector_class_init (ClipmanCollectorClass *klass)
{
  GObjectClass *object_class;

  g_type_class_add_private (klass, sizeof (ClipmanCollectorPrivate));

  clipman_collector_parent_class = g_type_class_peek_parent (klass);

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructed = clipman_collector_constructed;
  object_class->finalize = clipman_collector_finalize;
  object_class->set_property = clipman_collector_set_property;
  object_class->get_property = clipman_collector_get_property;

  g_object_class_install_property (object_class, ADD_PRIMARY_CLIPBOARD,
                                   g_param_spec_boolean ("add-primary-clipboard",
                                                         "AddPrimaryClipboard",
                                                         "Sync the primary clipboard with the default clipboard",
                                                         DEFAULT_ADD_PRIMARY_CLIPBOARD,
                                                         G_PARAM_CONSTRUCT|G_PARAM_READWRITE));

  g_object_class_install_property (object_class, HISTORY_IGNORE_PRIMARY_CLIPBOARD,
                                   g_param_spec_boolean ("history-ignore-primary-clipboard",
                                                         "HistoryIgnorePrimaryClipboard",
                                                         "Exclude the primary clipboard contents from the history",
                                                         DEFAULT_HISTORY_IGNORE_PRIMARY_CLIPBOARD,
                                                         G_PARAM_CONSTRUCT|G_PARAM_READWRITE));

  g_object_class_install_property (object_class, ENABLE_ACTIONS,
                                   g_param_spec_boolean ("enable-actions",
                                                         "EnableActions",
                                                         "Set to TRUE to enable actions (match the clipboard texts against regex's)",
                                                         DEFAULT_ENABLE_ACTIONS,
                                                         G_PARAM_CONSTRUCT|G_PARAM_READWRITE));

  g_object_class_install_property (object_class, INHIBIT,
                                   g_param_spec_boolean ("inhibit",
                                                         "Inhibit",
                                                         "Set to TRUE to disable the collector",
                                                         FALSE,
                                                         G_PARAM_CONSTRUCT|G_PARAM_READWRITE));
}

static void
clipman_collector_init (ClipmanCollector *collector)
{
  collector->priv = GET_PRIVATE (collector);

  /* This bit is set to TRUE when a clipboard has to be set from within clipman
   * while avoiding to re-add it to the history. */
  collector->priv->internal_change = FALSE;

  /* ClipmanActions */
  collector->priv->actions = clipman_actions_get ();

  /* ClipmanHistory */
  collector->priv->history = clipman_history_get ();

  /* Clipboards */
  collector->priv->default_clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  collector->priv->primary_clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
}

static void
clipman_collector_constructed (GObject *object)
{
  ClipmanCollector *collector = CLIPMAN_COLLECTOR (object);

  g_signal_connect_swapped (collector->priv->default_clipboard, "owner-change",
                            G_CALLBACK (cb_clipboard_owner_change), collector);
  g_signal_connect_swapped (collector->priv->primary_clipboard, "owner-change",
                            G_CALLBACK (cb_clipboard_owner_change), collector);
}

static void
clipman_collector_finalize (GObject *object)
{
  ClipmanCollector *collector = CLIPMAN_COLLECTOR (object);
  g_object_unref (collector->priv->history);
}

static void
clipman_collector_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  ClipmanCollectorPrivate *priv = CLIPMAN_COLLECTOR (object)->priv;

  switch (property_id)
    {
    case ADD_PRIMARY_CLIPBOARD:
      priv->add_primary_clipboard = g_value_get_boolean (value);
      break;

    case HISTORY_IGNORE_PRIMARY_CLIPBOARD:
      priv->history_ignore_primary_clipboard = g_value_get_boolean (value);
      break;

    case ENABLE_ACTIONS:
      priv->enable_actions = g_value_get_boolean (value);
      break;

    case INHIBIT:
      priv->inhibit = g_value_get_boolean (value);
      break;

    default:
      break;
    }
}

static void
clipman_collector_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  ClipmanCollectorPrivate *priv = CLIPMAN_COLLECTOR (object)->priv;

  switch (property_id)
    {
    case ADD_PRIMARY_CLIPBOARD:
      g_value_set_boolean (value, priv->add_primary_clipboard);
      break;

    case HISTORY_IGNORE_PRIMARY_CLIPBOARD:
      g_value_set_boolean (value, priv->history_ignore_primary_clipboard);
      break;

    case ENABLE_ACTIONS:
      g_value_set_boolean (value, priv->enable_actions);
      break;

    case INHIBIT:
      g_value_set_boolean (value, priv->inhibit);
      break;

    default:
      break;
    }
}
