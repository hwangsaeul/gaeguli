/**
 *  Copyright 2019 SK Telecom, Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 */

#include "config.h"

#include "pipeline.h"

static guint next_id = 0;

struct _GaeguliPipeline
{
  GObject parent;

  guint id;
  gchar *source;
  GHashTable *targets;
};

typedef enum
{
  PROP_ID = 1,
  PROP_SOURCE,

  /*< private > */
  PROP_LAST = PROP_SOURCE
} _GaeguliPipelineProperty;

static GParamSpec *properties[PROP_LAST + 1];

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliPipeline, gaeguli_pipeline, G_TYPE_OBJECT)
/* *INDENT-ON* */

static void
gaeguli_pipeline_dispose (GObject * object)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  g_clear_pointer (&self->targets, g_hash_table_unref);

  G_OBJECT_CLASS (gaeguli_pipeline_parent_class)->dispose (object);
}

static void
gaeguli_pipeline_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  switch ((_GaeguliPipelineProperty) prop_id) {
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;
    case PROP_SOURCE:
      g_value_set_string (value, self->source);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeguli_pipeline_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GaeguliPipeline *self = GAEGULI_PIPELINE (object);

  switch ((_GaeguliPipelineProperty) prop_id) {
    case PROP_SOURCE:
      g_assert (self->source == NULL);  /* construct only */
      self->source = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gaeguli_pipeline_class_init (GaeguliPipelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = gaeguli_pipeline_get_property;
  object_class->set_property = gaeguli_pipeline_set_property;
  object_class->dispose = gaeguli_pipeline_dispose;

  properties[PROP_ID] = g_param_spec_uint ("id", "id", "id",
      0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_SOURCE] = g_param_spec_string ("source", "source", "source",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties),
      properties);
}

static void
gaeguli_pipeline_init (GaeguliPipeline * self)
{
  g_atomic_int_inc (&next_id);
  self->id = next_id;

  self->targets = g_hash_table_new_full (g_variant_hash, g_variant_equal,
      (GDestroyNotify) g_variant_unref, NULL);
}

GaeguliPipeline *
gaeguli_pipeline_new (const gchar * source)
{
  g_autoptr (GaeguliPipeline) pipeline = NULL;

  /* TODO:
   *   1. check if source is valid
   *   2. check if source is available
   *
   * perphas, implement GInitable?
   */

  pipeline = g_object_new (GAEGULI_TYPE_PIPELINE, "source", source, NULL);

  return g_steal_pointer (&pipeline);
}

guint
gaeguli_pipeline_add_target (GaeguliPipeline * self,
    const gchar * host, guint port, GaeguliSRTMode mode, GError ** error)
{
  guint target_id = 0;
  g_autoptr (GVariant) hostinfo = NULL;

  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  hostinfo = g_variant_new ("(msii)", host, port, mode);
  target_id = g_variant_hash (hostinfo);

  return target_id;
}

GaeguliReturn
gaeguli_pipeline_remove_target (GaeguliPipeline * self, guint target_id,
    GError ** error)
{
  g_return_val_if_fail (GAEGULI_IS_PIPELINE (self), GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (target_id != 0, GAEGULI_RETURN_FAIL);
  g_return_val_if_fail (error == NULL || *error == NULL, GAEGULI_RETURN_FAIL);

  return GAEGULI_RETURN_OK;
}
