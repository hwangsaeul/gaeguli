/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "edge.h"

struct _GaeguliEdge
{
  GObject parent;
};


/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliEdge, gaeguli_edge, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void
gaeguli_edge_dispose (GObject * object)
{
  GaeguliEdge *self = GAEGULI_EDGE (object);

  G_OBJECT_CLASS (gaeguli_edge_parent_class)->dispose (object);
}

static void
gaeguli_edge_class_init (GaeguliEdgeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gaeguli_edge_dispose;
}

static void
gaeguli_edge_init (GaeguliEdge *self)
{
}

guint
gaeguli_start_stream (GaeguliEdge * self,
    const gchar * host, guint port, GaeguliSRTMode mode, GError ** error)
{
  guint stream_id = 0;

  g_return_val_if_fail (GAEGULI_IS_EDGE (self), 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  return stream_id;
}

GaeguliReturn
gaeguli_stop_stream (GaeguliEdge * self, guint stream_id)
{
  g_return_val_if_fail (GAEGULI_IS_EDGE (self), GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (stream_id != 0, GAEGULI_RETURN_FAIL);

  return GAEGULI_RETURN_OK;
}
