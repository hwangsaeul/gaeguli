/**
 *  Copyright 2020 SK Telecom Co., Ltd.
 *    Author: Jakub Adam <jakub.adam@collabora.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "traffic-control.h"

#include <netlink/route/qdisc/tbf.h>

struct _GaeguliTrafficControl
{
  GObject parent;

  gchar *interface;
  guint bandwidth;
  gboolean enabled;

  struct nl_sock *sock;
  struct rtnl_qdisc *qdisc;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeguliTrafficControl, gaeguli_traffic_control, G_TYPE_OBJECT)
/* *INDENT-ON* */

enum
{
  PROP_INTERFACE = 1,
  PROP_BANDWIDTH,
  PROP_ENABLED,
};

GaeguliTrafficControl *
gaeguli_traffic_control_new (const gchar * interface)
{
  return GAEGULI_TRAFFIC_CONTROL (g_object_new (GAEGULI_TYPE_TRAFFIC_CONTROL,
          "interface", interface, NULL));
}

static void
gaeguli_traffic_control_update_bandwidth (GaeguliTrafficControl * self)
{
  int res;

  if (self->enabled) {
    rtnl_qdisc_tbf_set_rate (self->qdisc, self->bandwidth / 8, 1500, 1);
    rtnl_qdisc_tbf_set_limit_by_latency (self->qdisc, 50000);

    res = rtnl_qdisc_add (self->sock, self->qdisc, NLM_F_CREATE);
    if (res != 0) {
      g_printerr ("rtnl_qdisc_add failed: %s\n", nl_geterror (res));
    }
  } else if (self->qdisc) {
    rtnl_qdisc_delete (self->sock, self->qdisc);
  }
}

static void
gaeguli_traffic_control_init (GaeguliTrafficControl * self)
{
}

static void
gaeguli_traffic_control_constructed (GObject * object)
{
  GaeguliTrafficControl *self = GAEGULI_TRAFFIC_CONTROL (object);

  struct nl_cache *cache;
  struct rtnl_link *link;
  int res;

  self->sock = nl_socket_alloc ();

  res = nl_connect (self->sock, NETLINK_ROUTE);
  if (res != 0) {
    g_printerr ("nl_connect failed: %s\n", nl_geterror (res));
    return;
  }

  rtnl_link_alloc_cache (self->sock, AF_UNSPEC, &cache);
  link = rtnl_link_get_by_name (cache, self->interface);

  self->qdisc = rtnl_qdisc_alloc ();
  rtnl_tc_set_ifindex (TC_CAST (self->qdisc), rtnl_link_get_ifindex (link));
  rtnl_tc_set_parent (TC_CAST (self->qdisc), TC_H_ROOT);
  rtnl_tc_set_kind (TC_CAST (self->qdisc), "tbf");

  rtnl_link_put (link);
  nl_cache_put (cache);
}

static void
gaeguli_traffic_control_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GaeguliTrafficControl *self = GAEGULI_TRAFFIC_CONTROL (object);

  switch (property_id) {
    case PROP_INTERFACE:
      self->interface = g_value_dup_string (value);
      break;
    case PROP_BANDWIDTH:{
      guint new_bandwidth = g_value_get_uint (value);

      if (self->bandwidth != new_bandwidth) {
        self->bandwidth = new_bandwidth;
        gaeguli_traffic_control_update_bandwidth (self);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    case PROP_ENABLED:{
      gboolean new_enabled = g_value_get_boolean (value);

      if (self->enabled != new_enabled) {
        self->enabled = new_enabled;
        gaeguli_traffic_control_update_bandwidth (self);
        g_object_notify_by_pspec (object, pspec);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gaeguli_traffic_control_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GaeguliTrafficControl *self = GAEGULI_TRAFFIC_CONTROL (object);

  switch (property_id) {
    case PROP_INTERFACE:
      g_value_set_string (value, self->interface);
      break;
    case PROP_BANDWIDTH:
      g_value_set_uint (value, self->bandwidth);
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gaeguli_traffic_control_dispose (GObject * object)
{
  GaeguliTrafficControl *self = GAEGULI_TRAFFIC_CONTROL (object);

  self->enabled = FALSE;
  gaeguli_traffic_control_update_bandwidth (self);

  g_clear_pointer (&self->qdisc, rtnl_qdisc_put);
  g_clear_pointer (&self->sock, nl_socket_free);
  g_clear_pointer (&self->interface, g_free);
}

static void
gaeguli_traffic_control_class_init (GaeguliTrafficControlClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gaeguli_traffic_control_set_property;
  gobject_class->get_property = gaeguli_traffic_control_get_property;
  gobject_class->constructed = gaeguli_traffic_control_constructed;
  gobject_class->dispose = gaeguli_traffic_control_dispose;

  g_object_class_install_property (gobject_class, PROP_INTERFACE,
      g_param_spec_string ("interface", "Network interface",
          "Network interface", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BANDWIDTH,
      g_param_spec_uint ("bandwidth", "Network bandwidth limit in bits/second",
          "Network bandwidth limit in bits/second", 1, G_MAXUINT, 1024000,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "Enable traffic control",
          "Enable traffic control", FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY |
          G_PARAM_STATIC_STRINGS));
}
