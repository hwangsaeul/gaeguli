/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "edge.h"
#include "pipeline.h"

#define DEFAULT_SOURCE  "/dev/video0"

struct _GaeguliEdge
{
  GObject parent;

  GHashTable *pipelines;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliEdge, gaeguli_edge, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void
gaeguli_edge_dispose (GObject * object)
{
  GaeguliEdge *self = GAEGULI_EDGE (object);

  g_clear_pointer (&self->pipelines, g_hash_table_unref);

  G_OBJECT_CLASS (gaeguli_edge_parent_class)->dispose (object);
}

static void
gaeguli_edge_class_init (GaeguliEdgeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gaeguli_edge_dispose;
}

static void
gaeguli_edge_init (GaeguliEdge * self)
{
  self->pipelines = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
}

guint
gaeguli_edge_start_stream (GaeguliEdge * self,
    const gchar * host, guint port, GaeguliSRTMode mode, GError ** error)
{
  guint pipeline_id = 0;
  g_autoptr (GaeguliPipeline) pipeline = NULL;

  g_return_val_if_fail (GAEGULI_IS_EDGE (self), 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

#if 0
  pipeline = gaeguli_pipeline_new ();
  g_object_get (pipeline, "id", &pipeline_id, NULL);

  if (!g_hash_table_insert (self->pipelines, g_strdup (DEFAULT_SOURCE),
          g_steal_pointer (&pipeline))) {
    g_error ("failed to create a pipeline connected to %s", DEFAULT_SOURCE);
    pipeline_id = 0;
  }
#endif // temporary disabled

  return pipeline_id;
}

static gboolean
_find_by_id (gpointer key, gpointer value, gpointer user_data)
{
  guint id = 0;
  GaeguliPipeline *pipeline = GAEGULI_PIPELINE (value);
  g_object_get (pipeline, "id", &id, NULL);

  return id == GPOINTER_TO_UINT (user_data);
}

GaeguliReturn
gaeguli_edge_stop_stream (GaeguliEdge * self, guint pipeline_id)
{
  GaeguliPipeline *pipeline = NULL;

  g_return_val_if_fail (GAEGULI_IS_EDGE (self), GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (pipeline_id != 0, GAEGULI_RETURN_FAIL);

  pipeline =
      g_hash_table_find (self->pipelines, _find_by_id,
      GUINT_TO_POINTER (pipeline_id));

  if (pipeline != NULL) {
    const gchar *source = NULL;
    g_object_get (pipeline, "source", &source, NULL);

    if (source == NULL || !g_hash_table_remove (self->pipelines, source)) {
      g_debug ("Cannot find a pipeline associated to %" G_GUINT32_FORMAT,
          pipeline_id);
    }
  }

  return GAEGULI_RETURN_OK;
}

GaeguliEdge *
gaeguli_edge_new (void)
{
  g_autoptr (GaeguliEdge) edge = NULL;

  edge = g_object_new (GAEGULI_TYPE_EDGE, NULL);

  return g_steal_pointer (&edge);
}
