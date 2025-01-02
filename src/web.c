#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <microhttpd.h>
#include <pthread.h>

typedef struct {
  struct MHD_Daemon *web_daemon;
  pthread_mutex_t mutex;
  int *note;
	float *cent;
	const char *plugin_path;
	bool server_running;
	bool muted;
} WebServer;

void web_server_init(WebServer *self, const char *plugin_path, int *note, float *cent) {
  self->plugin_path = plugin_path;
  self->note = note;
	self->cent = cent;
	self->server_running = false;
	self->muted = false;
}

static enum MHD_Result send_file_response(WebServer *self, struct MHD_Connection *connection, const char *file_name) {
  char *file_path = (char *)malloc(strlen(file_name) + strlen(self->plugin_path) + 1);
  strcpy(file_path, self->plugin_path);
  strcat(file_path, file_name);
    
  FILE *file = fopen(file_path, "r");
  if (file == NULL) {
    free(file_path);
    return MHD_NO;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *file_buffer = (char *)malloc(file_size + 1);
  if (file_buffer == NULL) {
    free(file_path);
    fclose(file);
    return MHD_NO;
  }

  fread(file_buffer, 1, file_size, file);
  fclose(file);
  file_buffer[file_size] = '\0';

  struct MHD_Response *response = MHD_create_response_from_buffer(file_size, file_buffer, MHD_RESPMEM_MUST_FREE);
  if (response == NULL) {
    free(file_path);
    free(file_buffer);
    return MHD_NO;
  }

  MHD_add_response_header(response, "Content-Type", "text/html");
  enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);

  free(file_path);

  return ret;
}

static enum MHD_Result answer_to_request(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls) {
	WebServer *self = (WebServer *)cls;
  if (strcmp(method, "GET") == 0) {
    char data[1024];
    if (strcmp(url, "/n") == 0)
      snprintf(data, sizeof(data), "%d", (int)*self->note);
    else if (strcmp(url, "/c") == 0)
      snprintf(data, sizeof(data), "%d", (int)*self->cent);
    else if (strcmp(url, "/m") == 0)
      self->muted = !self->muted;
    else 
      return send_file_response(self, connection, "index.html");
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(data), data, MHD_RESPMEM_PERSISTENT);
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
  }
  return (enum MHD_Result)MHD_HTTP_BAD_REQUEST;
}

void start_web_server(WebServer *self, int port) {
  pthread_mutex_init(&self->mutex, NULL);
  self->web_daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, port, NULL, NULL, &answer_to_request, self, MHD_OPTION_END);
  if (self->web_daemon == NULL)
    fprintf(stderr, "Failed to start the server.\n");
	else
		self->server_running = true;
}

void stop_web_server(WebServer *self) {
  MHD_stop_daemon(self->web_daemon);
  pthread_mutex_destroy(&self->mutex);
	self->server_running = false;
}

/* vi:set ts=2 sts=2 sw=2: */
