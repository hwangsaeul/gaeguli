/**
 *  Copyright 2019-2020 SK Telecom Co., Ltd.
 *    Author: Raghavendra Rao <raghavendra.rao@collabora.com>
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
 *
 */

#include "config.h"
#include <gaeguli/gaeguli.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/un.h>
#include <wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <poll.h>

#include <gio/gio.h>

int server_sockfd;
/* Assuming that there would be a single provider process for each camera device */
struct pollfd fds[GAEGULI_MAX_SOURCE_PROVIDERS + 1];
int numfds = 0;

void
_handle_child (int sig)
{
  int status;

  syslog (LOG_INFO, "Got sighup. Killing the child");

  wait (&status);
}

void
_stop_server ()
{
  unlink (DEFAULT_SOURCE_PROVIDER_CLIENT_SOCK_PATH);
  syslog (LOG_INFO, "Killing gaeguli_source_provider daemon with PID: %ld",
      (long) getpid ());
  kill (0, SIGKILL);
  exit (0);
}

static void
_handle_connection (int client_sockfd)
{
  int fd_idx = 0;
  int resfd = 0;

  while (1) {
    resfd = poll (fds, numfds, 10000);
    if (resfd < 0) {
      if (errno == EINTR) {
        syslog (LOG_ERR, "Got an interrupt. error (%s).", strerror (errno));
        goto exit;
      } else {
        syslog (LOG_ERR, "Error polling... error (%s)", strerror (errno));
        goto exit;
      }
    }

    for (fd_idx = 0; fd_idx < numfds; fd_idx++) {
      if (fds[fd_idx].revents & (POLLERR | POLLHUP)) {
        /* Check the socket error */
        int error = 0;
        socklen_t errlen = sizeof (error);
        getsockopt (fds[fd_idx].fd, SOL_SOCKET, SO_ERROR, (void *) &error,
            &errlen);
        /* Got POLLERR. Terminating */
        syslog (LOG_ERR, "Terminating on poll error. errnum %d error (%s)",
            error, strerror (errno));
        goto exit;
      } else if (fds[fd_idx].revents & POLLIN) {
        if (fds[fd_idx].fd == client_sockfd) {
          unsigned char data[64];
          int len = 0;
          GaeguliSourceProviderRsp *rsp = NULL;
          GaeguliSourceProviderMsg *msg = NULL;

          len = recv (client_sockfd, (void *) data, 64, 0);
          if (len > 0) {
            msg = (GaeguliSourceProviderMsg *) data;
            rsp = gaeguli_process_provider_message (msg);
            if (send (client_sockfd, (void *) rsp, sizeof (rsp), 0) < 0) {
              syslog (LOG_ERR, "Failed to send response on socket");
            }
            g_free (rsp);
          } else if (0 == len) {
            syslog (LOG_ERR, "Read from socket success. But read bytes = %d",
                len);
          } else {
            syslog (LOG_ERR, "Failed to read from socket. errnum %d error (%s)",
                errno, strerror (errno));
            goto exit;
          }
        }
      }
    }
  }
exit:
  close (client_sockfd);
  closelog ();
  exit (EXIT_SUCCESS);
}

int
main (int argc, char *argv[])
{
  int client_sockfd;
  struct sockaddr_un remote;
  socklen_t remote_size;
  int limit = GAEGULI_MAX_SOURCE_PROVIDERS + 1;
  int res = EXIT_FAILURE;

  gaeguli_daemonize ("gaeguli_source_provider");

  signal (SIGCHLD, _handle_child);
  signal (SIGTERM, _stop_server);

  if ((server_sockfd =
          gaeguli_init_socket (DEFAULT_SOURCE_PROVIDER_CLIENT_SOCK_PATH)) < 0) {
    syslog (LOG_ERR, "Failed to create server socket fd");
    goto out;
  }

  /* Prepare poll */
  fds[numfds].fd = server_sockfd;
  fds[numfds].events = POLLIN;
  fds[numfds].revents = 0;
  ++numfds;

  if (listen (server_sockfd, limit) < 0) {
    syslog (LOG_ERR, "listen failed. error (%s)", strerror (errno));
    goto out;
  }

  syslog (LOG_INFO, "gaeguli_source_provider daemon is ready. Listening ...");

  while (1) {
    pid_t pid;

    syslog (LOG_INFO,
        "gaeguli_source_provider daemon is waiting for a connection ...");
    remote_size = sizeof (remote);

    if ((client_sockfd =
            accept (server_sockfd, (struct sockaddr *) &remote,
                &remote_size)) < 0) {
      syslog (LOG_ERR, "accept failed. error (%s)", strerror (errno));
      goto out;
    }
    syslog (LOG_INFO,
        "gaeguli_source_provider daemon accepted a new connection where client_sockfd = %d, server_sockfd = %d",
        client_sockfd, server_sockfd);

    /* fork out to handle new connection */
    pid = gaeguli_handle_new_connection ();
    if (pid < 0) {
      goto out;
    } else if (0 == pid) {
      /* child process */
      fds[numfds].fd = client_sockfd;
      fds[numfds].events = POLLIN;
      fds[numfds].revents = 0;
      ++numfds;

      _handle_connection (client_sockfd);
      res = EXIT_SUCCESS;
      goto out;
    }
  }

out:
  syslog (LOG_INFO, "gaeguli_source_provider daemon terminated");
  close (server_sockfd);
  close (client_sockfd);
  closelog ();
  return res;
}
