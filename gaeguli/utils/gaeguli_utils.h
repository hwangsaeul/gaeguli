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

#ifndef __GAEGULI_UTILS_H__
#define __GAEGULI_UTILS_H__

typedef struct _GaeguliSourceProvider GaeguliSourceProvider;
typedef struct _GaeguliConsumer GaeguliConsumer;
typedef struct _GaeguliSourceProviderMsg GaeguliSourceProviderMsg;
typedef struct _GaeguliSourceProviderRsp GaeguliSourceProviderRsp;
typedef struct _GaeguliConsumerMsg GaeguliConsumerMsg;
typedef struct _GaeguliConsumerRsp GaeguliConsumerRsp;

GaeguliSourceProvider *
gaeguli_source_provider_new ();

int 							gaeguli_shm_mutex_unlock
													(pthread_mutex_t * lock);

int 							gaeguli_shm_mutex_lock 
													(pthread_mutex_t * lock);

int 							gaeguli_shm_mutex_close
													(const gchar * pfx,
													 guint node_id);

pthread_mutex_t 	 * 			gaeguli_shm_mutex_read
													(const gchar * pfx,
													 guint node_id);

pthread_mutex_t 	 * 			gaeguli_shm_mutex_new
													(const gchar * pfx,
													 guint node_id);

int 							gaeguli_send_socket_consumer_msg
													(GaeguliConsumerMsg *msg,
													 int fd);
int 							gaeguli_send_socket_provider_msg
													(GaeguliSourceProviderMsg *msg,
													 int fd);

int 							gaeguli_configure_provider_client_socket
													(void);

GaeguliSourceProviderRspType 	gaeguli_get_provider_daemon_response
													(int sockfd);

GaeguliSourceProviderRspType 	gaeguli_get_source_provider_rsptype
													(GaeguliSourceProviderRsp *rsp);

GaeguliSourceProviderMsgType 	gaeguli_get_source_provider_msgtype
													(GaeguliSourceProviderMsg *rsp);

GaeguliConsumerMsgType 			gaeguli_get_consumer_msgtype
													(GaeguliConsumerMsg *msg);

GaeguliConsumerRspType 			gaeguli_get_consumer_rsptype
													(GaeguliConsumerRsp *rsp);

GaeguliConsumerRspType 			gaeguli_get_consumer_daemon_response
													(int sockfd);

GaeguliPipeline 		*		gaeguli_source_provider_shm_new
													(guint node_id,
													 GaeguliPipeline * pipeline);

GaeguliPipeline 		* 		gaeguli_source_provider_shm_read
													(guint node_id);

int 							gaeguli_source_provider_shm_close
													(guint node_id);

int 							gaeguli_source_provider_shm_unmap
													(GaeguliPipeline * self,
													 int len);

int 							gaeguli_consumer_shm_new
													(guint node_id,
													 GaeguliTarget * target,
													 guint pfx);

GaeguliTarget 			* 		gaeguli_consumer_shm_read
													(guint pfx,
													 guint node_id);

int 							gaeguli_consumer_shm_unmap
													(GaeguliTarget * self,
													 int len);

int 							gaeguli_shm_mutex_unmap
													(pthread_mutex_t * lock,
													 int len);

int 							gaeguli_consumer_shm_close
													(guint pfx,
													 guint node_id);

GaeguliConsumerMsg 		* 		gaeguli_build_consumer_msg
													(GaeguliConsumerMsgType msg_type,
  													 GaeguliPipeline * pipeline,
  													 GaeguliVideoCodec codec,
  													 guint bitrate,
  													 const gchar * uri,
  													 const gchar * username,
  													 guint input_node_id,
  													 guint output_node_id,
  													 guint hash_id);

GaeguliSourceProviderMsg * 		gaeguli_build_source_provider_msg
													(GaeguliSourceProviderMsgType msg_type,
													 guint node_id);

GaeguliSourceProviderRsp *		gaeguli_build_source_provider_rsp
													(GaeguliSourceProviderRspType rsp_type);


GaeguliSourceProviderRsp * 		gaeguli_process_provider_message
													(GaeguliSourceProviderMsg *msg);

GaeguliConsumerRsp 		 *		gaeguli_process_consumer_message
													(GaeguliConsumerMsg *msg);

GaeguliPipeline 		 * 		gaeguli_get_pipeline
													(guint node_id);

void 							gaeguli_unmap_pipeline
													(GaeguliPipeline * pipeline);

int 							gaeguli_configure_client_socket
													(gchar * sock_path);

pid_t 							gaeguli_handle_new_connection
													(void);

int 							gaeguli_init_socket (gchar * sock_path);

void 							gaeguli_daemonize 	(gchar *tag);
#endif /* __GAEGULI_UTILS_H__ */