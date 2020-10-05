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

#include <netlink/route/qdisc/tbf.h>

#define HZ 100

int
main (int argc, char **argv)
{
  const char *interface;
  int bandwidth;

  struct nl_sock *sock;
  struct rtnl_qdisc *qdisc = NULL;
  struct nl_cache *cache;
  struct rtnl_link *link;
  int res = 0;

  if (argc != 3) {
    fprintf (stderr, "You must give interface name and bandwidth in bits per "
        "second as arguments\n");
    return -1;
  }

  interface = argv[1];
  bandwidth = atoi (argv[2]);

  sock = nl_socket_alloc ();

  res = nl_connect (sock, NETLINK_ROUTE);
  if (res != 0) {
    fprintf (stderr, "nl_connect failed: %s\n", nl_geterror (res));
    goto out;
  }

  rtnl_link_alloc_cache (sock, AF_UNSPEC, &cache);
  link = rtnl_link_get_by_name (cache, interface);

  qdisc = rtnl_qdisc_alloc ();
  rtnl_tc_set_ifindex (TC_CAST (qdisc), rtnl_link_get_ifindex (link));
  rtnl_tc_set_parent (TC_CAST (qdisc), TC_H_ROOT);
  rtnl_tc_set_kind (TC_CAST (qdisc), "tbf");

  rtnl_link_put (link);
  nl_cache_put (cache);

  if (bandwidth > 0) {
    rtnl_qdisc_tbf_set_rate (qdisc, bandwidth / 8, bandwidth, 1);
    rtnl_qdisc_tbf_set_limit_by_latency (qdisc, 50000);

    res = rtnl_qdisc_add (sock, qdisc, NLM_F_CREATE);
    if (res != 0) {
      fprintf (stderr, "rtnl_qdisc_add failed: %s\n", nl_geterror (res));
      goto out;
    }
  } else {
    rtnl_qdisc_delete (sock, qdisc);
  }

out:
  if (qdisc) {
    rtnl_qdisc_put (qdisc);
  }
  nl_socket_free (sock);

  return res;
}
