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
#include <gio/gio.h>

#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _GaeguliConsumer
{
  GaeguliPipeline *source_pipeline;     /* shared memory object */
  GaeguliTarget *target;        /* shared memory object */
  guint pipewire_input_node_id;
  guint pipewire_output_node_id;
};

struct _GaeguliSourceProviderMsg
{
  GaeguliSourceProviderMsgType msg_type;
  int pipewire_node_id;
};

struct _GaeguliSourceProviderRsp
{
  GaeguliSourceProviderRspType rsp_type;
};

struct _GaeguliConsumerMsg
{
  GaeguliConsumerMsgType msg_type;
  GaeguliVideoCodec codec;
  guint pipewire_input_node_id; /* pipewire node id of media.class = "Stream/Input/Video" *
                                 */
  guint pipewire_output_node_id;        /* pipewire node id of media.class = "Stream/Output/Video" *
                                         * w.r.t. pipewire node id of media.class = "Stream/Input/Video"  *
                                         */
  guint bitrate;
  guint hash_id;
  GaeguliPipeline *pipeline;
  gchar uri[128];
  gchar username[128];
  GError **error;
};

struct _GaeguliConsumerRsp
{
  GaeguliConsumerRspType rsp_type;
};

struct _GaeguliSharedTargets
{
  GHashTable *targets;
  guint num_active_targets;
};

#define DATA_SIZE   (256)

static const gchar *
get_runtime_dir (void)
{
  const gchar *runtime_dir;

  runtime_dir = getenv ("HOME");
  if (runtime_dir == NULL)
    runtime_dir = getenv ("USERPROFILE");
  return runtime_dir;
}

int
gaeguli_shm_target_close (const gchar * pfx, guint target_id)
{
  gchar shm_name[64];           /* Name of shared memory object */

  sprintf (shm_name, "%s_%u", pfx, target_id);

  /* remove the shared memory object */
  shm_unlink (shm_name);
  syslog (LOG_ERR, "Removed the shared memory object with name = %s", shm_name);
  return 0;
}

int
gaeguli_shm_target_unmap (GaeguliTarget * target, int len)
{
  /* unmap the shared memory of target */
  return munmap (target, len);
}

GaeguliTarget *
gaeguli_shm_target_new (const gchar * pfx, guint target_id)
{
  GaeguliTarget *target = NULL;
  gchar shm_name[64];           /* Name of shared memory object */
  gint shm_size = gaeguli_target_get_size ();   /* size of shared memory object */
  gint shm_fd;                  /* shared memory file descriptor */

  sprintf (shm_name, "%s_%u", pfx, target_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_CREAT | O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory target");
    return target;
  }

  /* set the size of shared memory object using ftruncate */
  if (ftruncate (shm_fd, shm_size) < 0) {
    syslog (LOG_ERR, "Failed to ftruncate shared memory fd");
    shm_unlink (shm_name);
    return target;
  }

  /* memory map the shared memory object */
  target =
      (GaeguliTarget *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);

  return target;
}

int
gaeguli_shm_mutex_unlock (pthread_mutex_t * lock)
{
  return pthread_mutex_unlock (lock);
}

int
gaeguli_shm_mutex_lock (pthread_mutex_t * lock)
{
  int result = pthread_mutex_lock (lock);

  if (result == EOWNERDEAD) {
    syslog (LOG_INFO, "pthread_mutex_lock() returned EOWNERDEAD");
    syslog (LOG_INFO, "Now make the mutex consistent");
    result = pthread_mutex_consistent (lock);
    if (0 != result) {
      syslog (LOG_ERR, "pthread_mutex_consistent failed");
    }
  }
  return result;
}

int
gaeguli_shm_mutex_unmap (pthread_mutex_t * lock, int len)
{
  /* destroy the mutex */
  pthread_mutex_destroy (lock);
  return munmap (lock, len);
}

int
gaeguli_shm_mutex_close (const gchar * pfx, guint node_id)
{
  gchar shm_name[64];           /* Name of shared memory object */

  sprintf (shm_name, "%s_%u", pfx, node_id);

  /* remove the shared memory object */
  syslog (LOG_ERR, "Removing the shared memory object with name = %s",
      shm_name);
  return shm_unlink (shm_name);
}

pthread_mutex_t *
gaeguli_shm_mutex_read (const gchar * pfx, guint node_id)
{
  gchar shm_name[64];
  guint shm_fd;
  gint shm_size = sizeof (pthread_mutex_t);
  pthread_mutex_t *lock = NULL;

  sprintf (shm_name, "%s_%u", pfx, node_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR, "Failed to open shared memory lock");
    return lock;
  }

  /* memory map the shared memory object */
  lock =
      (pthread_mutex_t *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);

  return lock;
}

pthread_mutex_t *
gaeguli_shm_mutex_new (const gchar * pfx, guint node_id)
{
  gchar shm_name[64];
  guint shm_fd;
  gint shm_size = sizeof (pthread_mutex_t);
  pthread_mutex_t *lock = NULL;
  pthread_mutexattr_t attr;

  sprintf (shm_name, "%s_%u", pfx, node_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_CREAT | O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory lock");
    return lock;
  }

  /* set the size of shared memory object using ftruncate */
  if (ftruncate (shm_fd, shm_size) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory fd");
    shm_unlink (shm_name);
    return lock;
  }

  /* memory map the shared memory object */
  lock =
      (pthread_mutex_t *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);

  pthread_mutexattr_init (&attr);
  pthread_mutexattr_setpshared (&attr, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_setrobust (&attr, PTHREAD_MUTEX_ROBUST);
  pthread_mutex_init (lock, &attr);

  return lock;
}

int
gaeguli_send_socket_consumer_msg (GaeguliConsumerMsg * msg, int fd)
{
  /* send socket IPC message to daemon service */
  int len;

  len = send (fd, (void *) msg, sizeof (GaeguliConsumerMsg), 0);
  g_free (msg);
  if (len < 0) {
    syslog (LOG_ERR, "Failed to send IPC msg");
  }

  return len;
}

int
gaeguli_send_socket_provider_msg (GaeguliSourceProviderMsg * msg, int fd)
{
  /* send socket IPC message to daemon service */
  int len;

  len = send (fd, (void *) msg, sizeof (GaeguliSourceProviderMsg), 0);
  g_free (msg);
  if (len < 0) {
    syslog (LOG_ERR, "Failed to send IPC msg");
  }

  return len;
}

pid_t
gaeguli_handle_new_connection (void)
{
  return fork ();
}

int
gaeguli_configure_client_socket (gchar * sock_path)
{
  struct sockaddr_un remote;
  gchar *runtime_dir;
  int sockfd, len;

  if ((sockfd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
    syslog (LOG_ERR, "Failed to create client socket");
    return -1;
  }

  syslog (LOG_INFO, "Trying to connect to daemon service...");

  runtime_dir = get_runtime_dir ();

  remote.sun_family = AF_UNIX;
  snprintf (remote.sun_path, sizeof (remote.sun_path), "%s/%s", runtime_dir,
      sock_path);
  len = strlen (remote.sun_path) + sizeof (remote.sun_family);

  if (connect (sockfd, (struct sockaddr *) &remote, len) == -1) {
    syslog (LOG_ERR, "Failed to connect client socket to daemon service");
    return -1;
  }

  syslog (LOG_INFO, "Successfully connected client socket with daemon service");
  return sockfd;
}

int
gaeguli_init_socket (gchar * sock_path)
{
  struct sockaddr_un local;
  gchar *runtime_dir;
  gint server_sockfd;
  int len;

  if ((server_sockfd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
    syslog (LOG_ERR, "Error creating server socket. error (%s)",
        strerror (errno));
    return -1;
  }

  runtime_dir = get_runtime_dir ();

  local.sun_family = AF_UNIX;
  snprintf (local.sun_path, sizeof (local.sun_path), "%s/%s", runtime_dir,
      sock_path);
  unlink (local.sun_path);
  len = strlen (local.sun_path) + sizeof (local.sun_family);
  if (bind (server_sockfd, (struct sockaddr *) &local, len) == -1) {
    syslog (LOG_ERR, "Failed to bind the server socket (%s). error (%s)",
        sock_path, strerror (errno));
    return -1;
  }

  return server_sockfd;
}

void
gaeguli_daemonize (gchar * tag)
{
  pid_t pid;
  int openfd;

  /* Fork off the parent process */
  pid = fork ();

  /* An error occurred */
  if (pid < 0) {
    syslog (LOG_ERR, "Failed to fork. error (%s)", strerror (errno));
    exit (EXIT_FAILURE);
  }

  if (pid > 0) {
    /* parent process */
    /* Success: Let the parent terminate */
    exit (EXIT_SUCCESS);
  }

  /* child process */
  /* Set new file permissions */
  umask (0);

  /* Open the log file */
  openlog (tag, LOG_PID | LOG_NDELAY | LOG_CONS, LOG_DAEMON);

  /* Create a new SID for the child process */
  /* On success: The child process becomes session leader */
  if (setsid () < 0) {
    syslog (LOG_ERR, "setsid() failed for child. PID: %ld error (%s)",
        (long) getpid (), strerror (errno));
    exit (EXIT_FAILURE);
  }

  /* Change the working directory to the root directory */
  /* or another appropriated directory */
  if (chdir ("/") < 0) {
    syslog (LOG_ERR, "chdir() failed for child. PID: %ld error (%s)",
        (long) getpid (), strerror (errno));
    exit (EXIT_FAILURE);
  }

  /* Close all open file descriptors */
  for (openfd = sysconf (_SC_OPEN_MAX); openfd >= 0; openfd--) {
    close (openfd);
  }
  syslog (LOG_INFO, "%s daemon started. PID: %ld", tag, (long) getpid ());
}

GaeguliConsumerRspType
gaeguli_get_consumer_daemon_response (int sockfd)
{
  struct pollfd fds[1];
  int numfds = 0;

  /* Prepare poll */
  fds[numfds].fd = sockfd;
  fds[numfds].events = POLLIN;
  fds[numfds].revents = 0;
  ++numfds;

  while (1) {
    int resfd = 0;

    /* poll on the fd */
    resfd = poll (fds, numfds, 10000);
    if (resfd < 0) {
      if (errno == EINTR) {
        syslog (LOG_ERR, "Got an interrupt. error (%s)", strerror (errno));
      } else {
        syslog (LOG_ERR, "Error polling... error (%s)", strerror (errno));
      }
      return GAEGULI_CONSUMER_RSP_FAIL;
    }

    if (fds[0].revents & (POLLERR | POLLHUP)) {
      /* Check the socket error */
      int error = 0;
      socklen_t errlen = sizeof (error);
      getsockopt (fds[0].fd, SOL_SOCKET, SO_ERROR, (void *) &error, &errlen);
      /* Got POLLERR. Terminating */
      syslog (LOG_ERR, "Poll error. errnum %d error (%s)\n", error,
          strerror (errno));
      return GAEGULI_CONSUMER_RSP_FAIL;
    } else if (fds[0].revents & POLLIN) {
      /* Got response from daemon */
      unsigned char data[DATA_SIZE];

      if (recv (fds[0].fd, (void *) data, DATA_SIZE, 0) < 0) {
        syslog (LOG_ERR, "Failed to recv msg from daemon. error (%s)",
            strerror (errno));
        return GAEGULI_CONSUMER_RSP_FAIL;
      } else {
        GaeguliConsumerRsp *rsp = (GaeguliConsumerRsp *) data;
        if (!rsp) {
          syslog (LOG_ERR, "Got NULL response from daemon");
          continue;
        }
        return gaeguli_get_consumer_rsptype (rsp);
      }
    }
  }
}

GaeguliSourceProviderRspType
gaeguli_get_provider_daemon_response (int sockfd)
{
  struct pollfd fds[1];
  int numfds = 0;

  /* Prepare poll */
  fds[numfds].fd = sockfd;
  fds[numfds].events = POLLIN;
  fds[numfds].revents = 0;
  ++numfds;

  while (1) {
    int resfd = 0;

    /* poll on the fd */
    resfd = poll (fds, numfds, 10000);
    if (resfd < 0) {
      if (errno == EINTR) {
        syslog (LOG_ERR, "Got an interrupt. error (%s)", strerror (errno));
      } else {
        syslog (LOG_ERR, "Error polling... error (%s)", strerror (errno));
      }
      return GAEGULI_SOURCE_PROVIDER_RSP_FAIL;
    }

    if (fds[0].revents & (POLLERR | POLLHUP)) {
      /* Check the socket error */
      int error = 0;
      socklen_t errlen = sizeof (error);
      getsockopt (fds[0].fd, SOL_SOCKET, SO_ERROR, (void *) &error, &errlen);
      /* Got POLLERR. Terminating */
      syslog (LOG_ERR, "Poll error. errnum %d error (%s)\n", error,
          strerror (errno));
      return GAEGULI_SOURCE_PROVIDER_RSP_FAIL;
    } else if (fds[0].revents & POLLIN) {
      /* Got response from daemon */
      unsigned char data[DATA_SIZE];

      if (recv (fds[0].fd, (void *) data, DATA_SIZE, 0) < 0) {
        syslog (LOG_ERR, "Failed to recv msg from daemon. error (%s)",
            strerror (errno));
        return GAEGULI_SOURCE_PROVIDER_RSP_FAIL;
      } else {
        GaeguliSourceProviderRsp *rsp = (GaeguliSourceProviderRsp *) data;
        if (!rsp) {
          syslog (LOG_ERR, "Got NULL response from daemon");
          continue;
        }
        return gaeguli_get_source_provider_rsptype (rsp);
      }
    }
  }
}

static GaeguliSourceProviderRsp *
_process_provider_message (GaeguliSourceProviderMsg * msg)
{
  GaeguliSourceProviderRsp *rsp = NULL;

  if (!msg) {
    return rsp;
  }

  switch (msg->msg_type) {
    case GAEGULI_SOURCE_PROVIDER_MSG_CREATE_PIPELINE:{
      GaeguliPipeline *pipeline = gaeguli_create_pipeline (DEFAULT_VIDEO_SOURCE,
          msg->pipewire_node_id, DEFAULT_VIDEO_RESOLUTION,
          DEFAULT_VIDEO_FRAMERATE);

      if (pipeline) {
        /* Write the GaeguliPipeline instance into shared memory */
        if (gaeguli_source_provider_shm_new (msg->pipewire_node_id,
                pipeline) < 0) {
          syslog (LOG_ERR,
              "Failed to write the GaeguliPipeline instance [%p] into shared memory",
              pipeline);
          break;
        }

        rsp = g_new0 (GaeguliSourceProviderRsp, 1);
        rsp->rsp_type = GAEGULI_SOURCE_PROVIDER_RSP_CREATE_SUCCESS;
      } else {
        syslog (LOG_ERR, "Failed to create GaeguliPipeline instance");
      }
    }
      break;
    case GAEGULI_SOURCE_PROVIDER_MSG_DESTROY_PIPELINE:{
      /* Read the GaeguliPipeline instance from shared memory */
      GaeguliPipeline *pipeline =
          gaeguli_source_provider_shm_read (msg->pipewire_node_id);

      if (pipeline) {
        /* Destroy the source provider pipeline */
        gaeguli_destroy_pipeline (pipeline);
        /* Remove the shared memory object */
        if (gaeguli_source_provider_shm_unmap (pipeline,
                gaeguli_pipeline_get_size ()) < 0) {
          syslog (LOG_ERR, "Failed to unmap the shared memory");
        }
        gaeguli_source_provider_shm_close (msg->pipewire_node_id);
        rsp = g_new0 (GaeguliSourceProviderRsp, 1);
        rsp->rsp_type = GAEGULI_SOURCE_PROVIDER_RSP_DESTROY_SUCCESS;
      } else {
        syslog (LOG_ERR,
            "Failed to get the GaeguliPipeline instance from shared memory");
      }
    }
      break;

    default:
      break;
  }
  return rsp;
}

GaeguliSourceProviderRsp *
gaeguli_process_provider_message (GaeguliSourceProviderMsg * msg)
{
  return _process_provider_message (msg);
}

static GaeguliConsumerRsp *
_process_consumer_message (GaeguliConsumerMsg * msg)
{
  GaeguliConsumerRsp *rsp = NULL;
  g_autoptr (GError) error = NULL;

  if (!msg) {
    return rsp;
  }

  switch (msg->msg_type) {
    case GAEGULI_CONSUMER_MSG_CREATE_SRT_TARGET:{
      GaeguliPipeline *pipeline = NULL;
      GaeguliTarget *target = NULL;

      pipeline = gaeguli_source_provider_shm_read (msg->pipewire_input_node_id);
      if (!pipeline) {
        syslog (LOG_ERR,
            "Failed to get the GaeguliPipeline instance from shared memory");
        return rsp;
      }

      target = gaeguli_pipeline_add_target (pipeline, msg->codec,
          msg->bitrate, msg->uri, msg->username, NULL,
          GAEGULI_CONSUMER_MSG_CREATE_SRT_TARGET, msg->pipewire_input_node_id,
          msg->pipewire_output_node_id, &error);

      if (target) {
        /* Write the GaeguliTarget instance into shared memory */
        if (gaeguli_consumer_shm_new (msg->pipewire_output_node_id, target,
                g_str_hash (msg->uri)) < 0) {
          syslog (LOG_ERR,
              "Failed to write the GaeguliTarget instance [%p] into shared memory",
              target);
          break;
        }

        if (gaeguli_source_provider_shm_unmap (pipeline,
                gaeguli_pipeline_get_size ()) < 0) {
          syslog (LOG_ERR, "Failed to unmap GaeguliPipeline instance");
        }

        rsp = g_new0 (GaeguliConsumerRsp, 1);
        rsp->rsp_type = GAEGULI_CONSUMER_RSP_CREATE_TARGET_SUCCESS;
      } else {
        syslog (LOG_ERR, "Failed to create GaeguliTarget instance");
        rsp = g_new0 (GaeguliConsumerRsp, 1);
        rsp->rsp_type = GAEGULI_CONSUMER_RSP_FAIL;
      }
    }
      break;

    case GAEGULI_CONSUMER_MSG_START_TARGET:{
      GaeguliTarget *target = NULL;

      /* Get the GaeguliTarget instance from shared memory */
      target =
          gaeguli_consumer_shm_read (msg->hash_id,
          msg->pipewire_output_node_id);
      if (!target) {
        syslog (LOG_ERR, "No active target available with id = %u",
            g_str_hash (msg->uri));
        break;
      }

      rsp = g_new0 (GaeguliConsumerRsp, 1);
      rsp->rsp_type = gaeguli_start_consumer (target, &error);

      /* FIXME - This crashes some times with srt target */
      // if (gaeguli_consumer_shm_unmap (target, gaeguli_target_get_size ()) < 0) {
      //   syslog (LOG_ERR, "In %s. Failed to unmap the shared memory. Error (%s)",
      //     __FUNCTION__, strerror (errno));
      // } else {
      //   syslog (LOG_ERR, "In %s. Unmapped the shared memory", __FUNCTION__);
      // }
    }
      break;

    case GAEGULI_CONSUMER_MSG_CREATE_RECORDING_TARGET:{
      GaeguliPipeline *pipeline =
          gaeguli_source_provider_shm_read (msg->pipewire_input_node_id);
      GaeguliTarget *target = gaeguli_pipeline_add_target (pipeline, msg->codec,
          msg->bitrate, NULL, NULL, msg->uri,
          GAEGULI_CONSUMER_MSG_CREATE_RECORDING_TARGET,
          msg->pipewire_input_node_id,
          msg->pipewire_output_node_id, &error);

      if (target) {
        /* Write the GaeguliTarget instance into shared memory */
        if (gaeguli_consumer_shm_new (msg->pipewire_output_node_id, target,
                g_str_hash (msg->uri)) < 0) {
          syslog (LOG_ERR,
              "Failed to write the GaeguliTarget instance [%p] into shared memory",
              target);
          break;
        }

        rsp = g_new0 (GaeguliConsumerRsp, 1);
        rsp->rsp_type = GAEGULI_CONSUMER_RSP_CREATE_TARGET_SUCCESS;

        if (gaeguli_consumer_shm_unmap (target, gaeguli_target_get_size ()) < 0) {
          syslog (LOG_ERR, "Failed to unmap the shared memory. Error (%s)",
              strerror (errno));
        }
      } else {
        syslog (LOG_ERR, "Failed to create GaeguliTarget instance");
      }
    }
      break;

    case GAEGULI_CONSUMER_MSG_CREATE_IMGCAPTURE_TARGET:{
      GaeguliPipeline *pipeline =
          gaeguli_source_provider_shm_read (msg->pipewire_input_node_id);
      GaeguliTarget *target = gaeguli_pipeline_add_target (pipeline, msg->codec,
          msg->bitrate, NULL, NULL, msg->uri,
          GAEGULI_CONSUMER_MSG_CREATE_IMGCAPTURE_TARGET,
          msg->pipewire_input_node_id,
          msg->pipewire_output_node_id, &error);

      if (target) {
        /* Write the GaeguliTarget instance into shared memory */
        if (gaeguli_consumer_shm_new (msg->pipewire_output_node_id, target,
                g_str_hash ("image_capture")) < 0) {
          syslog (LOG_ERR,
              "Failed to write the GaeguliTarget instance [%p] into shared memory",
              target);
          break;
        }

        rsp = g_new0 (GaeguliConsumerRsp, 1);
        rsp->rsp_type = GAEGULI_CONSUMER_RSP_CREATE_TARGET_SUCCESS;

        if (gaeguli_consumer_shm_unmap (target, gaeguli_target_get_size ()) < 0) {
          syslog (LOG_ERR, "Failed to unmap the shared memory. Error (%s)",
              strerror (errno));
        }
      } else {
        syslog (LOG_ERR, "Failed to create GaeguliTarget instance");
      }
    }
      break;

    case GAEGULI_CONSUMER_MSG_DESTROY_TARGET:{
      /* Read the GaeguliPipeline instance from shared memory */
      GaeguliTarget *target = gaeguli_consumer_shm_read (msg->hash_id,
          msg->pipewire_output_node_id);

      if (target) {
        /* Destroy the source provider pipeline */
        gaeguli_target_stop (target);
        /* Remove the shared memory object */
        if (gaeguli_consumer_shm_unmap (target, gaeguli_target_get_size ()) < 0) {
          syslog (LOG_ERR, "Failed to unmap the shared memory. Error (%s)",
              strerror (errno));
        }

        gaeguli_consumer_shm_close (msg->hash_id, msg->pipewire_output_node_id);

        rsp = g_new0 (GaeguliConsumerRsp, 1);
        rsp->rsp_type = GAEGULI_CONSUMER_RSP_DESTROY_TARGET_SUCCESS;
      } else {
        syslog (LOG_ERR,
            "Failed to get the GaeguliPipeline instance from shared memory");
      }
    }
      break;

    default:
      break;
  }
  return rsp;
}

GaeguliConsumerRsp *
gaeguli_process_consumer_message (GaeguliConsumerMsg * msg)
{
  return _process_consumer_message (msg);
}

static GaeguliSourceProviderMsg *
_build_source_provider_msg (GaeguliSourceProviderMsgType msg_type,
    guint node_id)
{
  GaeguliSourceProviderMsg *msg = NULL;
  msg = g_new0 (GaeguliSourceProviderMsg, 1);

  msg->msg_type = msg_type;
  msg->pipewire_node_id = node_id;

  return msg;
}

GaeguliSourceProviderMsg *
gaeguli_build_source_provider_msg (GaeguliSourceProviderMsgType msg_type,
    guint node_id)
{
  return _build_source_provider_msg (msg_type, node_id);
}

static GaeguliConsumerMsg *
_build_consumer_msg (GaeguliConsumerMsgType msg_type,
    GaeguliPipeline * pipeline, GaeguliVideoCodec codec,
    guint bitrate, const gchar * uri, const gchar * username,
    guint input_node_id, guint output_node_id, guint hash_id)
{
  GaeguliConsumerMsg *msg = NULL;
  msg = g_new0 (GaeguliConsumerMsg, 1);

  msg->msg_type = msg_type;
  msg->pipeline = pipeline;
  msg->codec = codec;
  msg->bitrate = bitrate;
  msg->pipewire_input_node_id = input_node_id;
  msg->pipewire_output_node_id = output_node_id;
  msg->hash_id = hash_id;

  if (uri) {
    strncpy (msg->uri, uri, strlen (uri));
  }
  if (username) {
    strncpy (msg->username, username, strlen (uri));
  }

  return msg;
}

GaeguliConsumerMsg *
gaeguli_build_consumer_msg (GaeguliConsumerMsgType msg_type,
    GaeguliPipeline * pipeline, GaeguliVideoCodec codec,
    guint bitrate, const gchar * uri, const gchar * username,
    guint input_node_id, guint output_node_id, guint hash_id)
{
  return _build_consumer_msg (msg_type, pipeline, codec,
      bitrate, uri, username, input_node_id, output_node_id, hash_id);
}

static GaeguliSourceProviderRsp *
_build_source_provider_rsp (GaeguliSourceProviderRspType rsp_type)
{
  GaeguliSourceProviderRsp *rsp = NULL;
  rsp = g_new0 (GaeguliSourceProviderRsp, 1);

  rsp->rsp_type = rsp_type;

  return rsp;
}

GaeguliSourceProviderRsp *
gaeguli_build_source_provider_rsp (GaeguliSourceProviderRspType rsp_type)
{
  return _build_source_provider_rsp (rsp_type);
}

static GaeguliConsumerRsp *
_build_consumer_rsp (GaeguliConsumerRspType rsp_type)
{
  GaeguliConsumerRsp *rsp = NULL;
  rsp = g_new0 (GaeguliConsumerRsp, 1);

  rsp->rsp_type = rsp_type;

  return rsp;
}

GaeguliConsumerRsp *
gaeguli_build_consumer_rsp (GaeguliConsumerRspType rsp_type)
{
  return _build_consumer_rsp (rsp_type);
}

GaeguliConsumerRspType
gaeguli_get_consumer_rsptype (GaeguliConsumerRsp * rsp)
{
  return rsp->rsp_type;
}

GaeguliSourceProviderRspType
gaeguli_get_source_provider_rsptype (GaeguliSourceProviderRsp * rsp)
{
  return rsp->rsp_type;
}

GaeguliSourceProviderMsgType
gaeguli_get_source_provider_msgtype (GaeguliSourceProviderMsg * msg)
{
  return msg->msg_type;
}

GaeguliPipeline *
gaeguli_get_source_provider_pipeline (GaeguliSourceProviderRsp * rsp)
{
  return NULL;
}

int
gaeguli_consumer_shm_unmap (GaeguliTarget * self, int len)
{
  return munmap (self, len);
}

GaeguliTarget *
gaeguli_consumer_shm_read (guint pfx, guint node_id)
{
  GaeguliTarget *target = NULL;
  gchar shm_name[64];           /* Name of shared memory object */
  gint shm_size = gaeguli_target_get_size ();   /* size of shared memory object */
  gint shm_fd;                  /* shared memory file descriptor */

  sprintf (shm_name, "%u_%u", pfx, node_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR, "Failed to open shared memory object to read");
    return NULL;
  }

  /* memory map the shared memory object */
  target =
      (GaeguliTarget *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);

  return target;
}

int
gaeguli_consumer_shm_close (guint pfx, guint node_id)
{
  gchar shm_name[64];           /* Name of shared memory object */
  GaeguliTarget *self = NULL;

  sprintf (shm_name, "%u_%u", pfx, node_id);

  self = gaeguli_consumer_shm_read (pfx, node_id);

  gaeguli_target_free_srt_resources (self);

  if (gaeguli_consumer_shm_unmap (self, gaeguli_target_get_size ()) < 0) {
    syslog (LOG_ERR, "Failed to unmap the shared memory object with name = %s",
        shm_name);
  }

  /* remove the shared memory object */
  if (shm_unlink (shm_name) < 0) {
    syslog (LOG_ERR, "Failed to unlink the shared memory object with name = %s",
        shm_name);
  }
  return 0;
}

int
gaeguli_consumer_shm_new (guint node_id, GaeguliTarget * target, guint pfx)
{
  gchar shm_name[64];           /* Name of shared memory object */
  gint shm_size = gaeguli_target_get_size ();   /* size of shared memory object */
  gint shm_fd;                  /* shared memory file descriptor */
  GaeguliTarget *shm_ptr = NULL;        /* pointer to shared memory object */

  sprintf (shm_name, "%u_%u", pfx, node_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_CREAT | O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory fd");
    goto err;
  }

  /* set the size of shared memory object using ftruncate */
  if (ftruncate (shm_fd, shm_size) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory fd");
    goto err;
  }

  /* memory map the shared memory object */
  shm_ptr =
      (GaeguliTarget *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);
  /* Write the GaeguliTarget instance to the shared memory object */
  gaeguli_target_deep_copy (target, shm_ptr);

  if (gaeguli_consumer_shm_unmap (shm_ptr, gaeguli_target_get_size ()) < 0) {
    syslog (LOG_ERR, "Failed to unmap the shared memory");
  }
  return 0;
err:
  /* remove the shared memory object on error */
  syslog (LOG_ERR, "Removed the shared memory object with name = %s on error",
      shm_name);
  shm_unlink (shm_name);
  return -1;
}

int
gaeguli_source_provider_shm_close (guint node_id)
{
  gchar shm_name[64];           /* Name of shared memory object */

  sprintf (shm_name, "%u", node_id);

  /* remove the shared memory object */
  if (shm_unlink (shm_name) < 0) {
    syslog (LOG_ERR, "Failed to unlink the shared memory. Error (%s)",
        strerror (errno));
  }
  return 0;
}

int
gaeguli_source_provider_shm_unmap (GaeguliPipeline * self, int len)
{
  return munmap (self, len);
}

GaeguliPipeline *
gaeguli_source_provider_shm_read (guint node_id)
{
  GaeguliPipeline *pipeline = NULL;
  gchar shm_name[64];           /* Name of shared memory object */
  gint shm_size = gaeguli_pipeline_get_size (); /* size of shared memory object */
  gint shm_fd;                  /* shared memory file descriptor */

  sprintf (shm_name, "%u", node_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR,
        "Failed to open shared memory object to read where name = %s",
        shm_name);
    return NULL;
  }

  /* memory map the shared memory object */
  pipeline =
      (GaeguliPipeline *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);

  return pipeline;
}

GaeguliPipeline *
gaeguli_source_provider_shm_new (guint node_id, GaeguliPipeline * pipeline)
{
  gchar shm_name[64];           /* Name of shared memory object */
  gint shm_size = gaeguli_pipeline_get_size (); /* size of shared memory object */
  gint shm_fd;                  /* shared memory file descriptor */
  GaeguliPipeline *shm_ptr = NULL;      /* pointer to shared memory object */

  sprintf (shm_name, "%u", node_id);

  /* open the shared memory object */
  if ((shm_fd = shm_open (shm_name, O_CREAT | O_RDWR, 0666)) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory fd");
    goto error;
  }

  /* set the size of shared memory object using ftruncate */
  if (ftruncate (shm_fd, shm_size) < 0) {
    syslog (LOG_ERR, "Failed to create shared memory fd");
    goto error;
  }

  /* memory map the shared memory object */
  shm_ptr =
      (GaeguliPipeline *) mmap (0, shm_size, PROT_WRITE, MAP_SHARED, shm_fd, 0);
  /* Write the GaeguliPipeline instance to the shared memory object */
  gaeguli_pipeline_deep_copy (pipeline, shm_ptr);

  return shm_ptr;
error:
  /* remove the shared memory object on error */
  syslog (LOG_ERR, "Removed the shared memory object with name = %s on error",
      shm_name);
  shm_unlink (shm_name);
  return NULL;
}
