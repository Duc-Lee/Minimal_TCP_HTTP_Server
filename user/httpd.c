// web server don gian chay http 1.0 phuc vu file tinh

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define HTTP_PORT 80
#define BUF_SIZE  1024

// xu ly request http 1.0 tu client
void
handle_http_client(int client_fd)
{
  char req[BUF_SIZE];
  int n = read(client_fd, req, sizeof(req) - 1);
  if (n <= 0) {
    close(client_fd);
    return;
  }
  req[n] = '\0';

  // chi ho tro GET
  if (req[0] != 'G' || req[1] != 'E' || req[2] != 'T' || req[3] != ' ') {
    const char *bad = "HTTP/1.0 400 Bad Request\r\n\r\n";
    write(client_fd, bad, strlen(bad));
    close(client_fd);
    return;
  }

  // parse lay path file can lay
  char path[128];
  int i = 4, j = 0;
  if (req[i] == '/')
    i++;

  while (req[i] != ' ' && req[i] != '\r' && req[i] != '\n' && req[i] != '\0' &&
         j < sizeof(path) - 1) {
    path[j++] = req[i++];
  }
  path[j] = '\0';

  if (j == 0) {
    strcpy(path, "README");
  }

  // mo file tren he thong file
  int file_fd = open(path, O_RDONLY);
  if (file_fd < 0) {
    const char *not_found =
      "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\n404 Not Found";
    write(client_fd, not_found, strlen(not_found));
    close(client_fd);
    return;
  }

  // gui header 200 ok
  const char *hdr =
    "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n";
  write(client_fd, hdr, strlen(hdr));

  // doc va truyen du lieu theo chunk
  char file_buf[BUF_SIZE];
  int bytes_read;
  while ((bytes_read = read(file_fd, file_buf, sizeof(file_buf))) > 0) {
    int bytes_sent = 0;
    while (bytes_sent < bytes_read) {
      int w = write(client_fd, file_buf + bytes_sent, bytes_read - bytes_sent);
      if (w <= 0)
        break;
      bytes_sent += w;
    }
    if (bytes_sent < bytes_read)
      break;
  }

  close(file_fd);
  close(client_fd);
}

int
main(int argc, char *argv[])
{
  int port = HTTP_PORT;
  if (argc > 1)
    port = atoi(argv[1]);

  printf("httpd: listening on port %d\n", port);

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    printf("httpd: socket failed\n");
    exit(1);
  }

  if (bind(listen_fd, INADDR_ANY, port) < 0) {
    printf("httpd: bind failed\n");
    close(listen_fd);
    exit(1);
  }

  if (listen(listen_fd, 5) < 0) {
    printf("httpd: listen failed\n");
    close(listen_fd);
    exit(1);
  }

  while (1) {
    int client_fd = accept(listen_fd);
    if (client_fd < 0)
      continue;
    handle_http_client(client_fd);
  }

  exit(0);
}
