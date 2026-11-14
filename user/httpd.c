// web server ho tro http/1.1, pre-fork worker pool, keep-alive va tuong thich trinh duyet

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define HTTP_PORT         80
#define NWORKERS          4
#define BACKLOG           16
#define REQ_BUF_SIZE      2048
#define MAX_REQ_PER_CONN  16

// bo dem nhan request va doc file trong vung BSS de tranh tran user stack 4KB
static char req_buf[REQ_BUF_SIZE];
static char file_buf[4096];

// tim chuoi con khong phan biet hoa thuong
static const char *
find_substr_ci(const char *haystack, const char *needle)
{
  int nlen = strlen(needle);
  for (int i = 0; haystack[i]; i++) {
    int match = 1;
    for (int j = 0; j < nlen; j++) {
      char c1 = haystack[i + j];
      char c2 = needle[j];
      if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
      if (c2 >= 'A' && c2 <= 'Z')
        c2 += 32;
      if (c1 != c2) {
        match = 0;
        break;
      }
    }
    if (match)
      return haystack + i;
  }
  return 0;
}

// ham ho tro ghep chuoi an toan vao buffer
static int
str_append(char *dst, int dst_size, int cur_len, const char *src)
{
  while (*src && cur_len < dst_size - 1) {
    dst[cur_len++] = *src++;
  }
  dst[cur_len] = '\0';
  return cur_len;
}

// ghep so nguyen duong vao chuoi
static int
int_append(char *dst, int dst_size, int cur_len, uint n)
{
  char tmp[16];
  int i = 0;
  if (n == 0) {
    return str_append(dst, dst_size, cur_len, "0");
  }
  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }
  while (i > 0 && cur_len < dst_size - 1) {
    dst[cur_len++] = tmp[--i];
  }
  dst[cur_len] = '\0';
  return cur_len;
}

// xac dinh content-type theo phan mo rong cua file
static const char *
get_mime_type(const char *path)
{
  int len = strlen(path);
  if (len >= 5 && strcmp(path + len - 5, ".html") == 0)
    return "text/html; charset=utf-8";
  if (len >= 4 && strcmp(path + len - 4, ".htm") == 0)
    return "text/html; charset=utf-8";
  if (len >= 4 && strcmp(path + len - 4, ".css") == 0)
    return "text/css";
  if (len >= 3 && strcmp(path + len - 3, ".js") == 0)
    return "application/javascript";
  if (len >= 4 && strcmp(path + len - 4, ".png") == 0)
    return "image/png";
  if (len >= 4 && strcmp(path + len - 4, ".jpg") == 0)
    return "image/jpeg";
  if (len >= 5 && strcmp(path + len - 5, ".jpeg") == 0)
    return "image/jpeg";
  if (len >= 4 && strcmp(path + len - 4, ".ico") == 0)
    return "image/x-icon";
  if (len >= 4 && strcmp(path + len - 4, ".txt") == 0)
    return "text/plain; charset=utf-8";
  if (strcmp(path, "README") == 0)
    return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

// xu ly ket noi client voi ho tro http/1.1 va keep-alive
static void
handle_http_client(int client_fd)
{
  int req_count = 0;
  int keep_alive = 1;

  while (keep_alive && req_count < MAX_REQ_PER_CONN) {
    int n = read(client_fd, req_buf, sizeof(req_buf) - 1);
    if (n <= 0)
      break;
    req_buf[n] = '\0';
    req_count++;

    // kiem tra phuong thuc http: chi ho tro GET hoac HEAD
    int is_head = 0;
    int i = 0;
    if (memcmp(req_buf, "GET ", 4) == 0) {
      i = 4;
    } else if (memcmp(req_buf, "HEAD ", 5) == 0) {
      is_head = 1;
      i = 5;
    } else {
      const char *bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      write(client_fd, bad, strlen(bad));
      break;
    }

    // bo qua khoang trang dau
    while (req_buf[i] == ' ')
      i++;

    // bo qua dau gach cheo dau tien
    while (req_buf[i] == '/')
      i++;

    // trich xuat duong dan file (dung lai o khoang trang, ?, \r, \n hoac ket thuc chuoi)
    char path[64];
    int j = 0;
    while (req_buf[i] != ' ' && req_buf[i] != '?' && req_buf[i] != '\r' &&
           req_buf[i] != '\n' && req_buf[i] != '\0' && j < sizeof(path) - 1) {
      path[j++] = req_buf[i++];
    }
    path[j] = '\0';

    // bo qua query parameter neu co
    while (req_buf[i] != ' ' && req_buf[i] != '\r' && req_buf[i] != '\n' && req_buf[i] != '\0')
      i++;
    while (req_buf[i] == ' ')
      i++;

    // kiem tra phien ban http
    int is_http11 = (memcmp(req_buf + i, "HTTP/1.1", 8) == 0);

    // xac dinh trang thai keep-alive dua tren header
    int has_close = (find_substr_ci(req_buf, "connection: close") != 0);
    int has_keep_alive = (find_substr_ci(req_buf, "connection: keep-alive") != 0);

    if (has_close) {
      keep_alive = 0;
    } else if (has_keep_alive) {
      keep_alive = 1;
    } else if (is_http11) {
      keep_alive = 1; // http/1.1 mac dinh la keep-alive
    } else {
      keep_alive = 0; // http/1.0 mac dinh la dong ket noi
    }

    // dong ket noi neu da dat toi han so request tren 1 ket noi
    if (req_count >= MAX_REQ_PER_CONN)
      keep_alive = 0;

    // mac dinh phuc vu index.html neu truy cap goc /
    if (path[0] == '\0') {
      struct stat st;
      if (stat("index.html", &st) == 0)
        strcpy(path, "index.html");
      else
        strcpy(path, "README");
    }

    // xu ly yeu cau /favicon.ico: tra ve 204 No Content de trinh duyet khong bao loi 404
    if (strcmp(path, "favicon.ico") == 0) {
      char hdr[256];
      int hlen = 0;
      if (is_http11) {
        hlen = str_append(hdr, sizeof(hdr), hlen, "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: ");
        hlen = str_append(hdr, sizeof(hdr), hlen, keep_alive ? "keep-alive\r\n\r\n" : "close\r\n\r\n");
      } else {
        hlen = str_append(hdr, sizeof(hdr), hlen, "HTTP/1.0 204 No Content\r\nContent-Length: 0\r\n\r\n");
      }
      write(client_fd, hdr, hlen);
      continue;
    }

    // mo file tren he thong file xv6
    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
      char hdr[256];
      int hlen = 0;
      const char *body = "404 Not Found";
      int blen = strlen(body);
      if (is_http11) {
        hlen = str_append(hdr, sizeof(hdr), hlen, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ");
        hlen = int_append(hdr, sizeof(hdr), hlen, blen);
        hlen = str_append(hdr, sizeof(hdr), hlen, "\r\nConnection: ");
        hlen = str_append(hdr, sizeof(hdr), hlen, keep_alive ? "keep-alive\r\n\r\n" : "close\r\n\r\n");
      } else {
        hlen = str_append(hdr, sizeof(hdr), hlen, "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ");
        hlen = int_append(hdr, sizeof(hdr), hlen, blen);
        hlen = str_append(hdr, sizeof(hdr), hlen, "\r\n\r\n");
      }
      write(client_fd, hdr, hlen);
      if (!is_head)
        write(client_fd, body, blen);
      continue;
    }

    // lay thong tin kich thuoc file de dien Content-Length
    struct stat st;
    if (fstat(file_fd, &st) < 0)
      st.size = 0;

    // tao header phan hoi 200 OK
    char hdr[512];
    int hlen = 0;
    if (is_http11) {
      hlen = str_append(hdr, sizeof(hdr), hlen, "HTTP/1.1 200 OK\r\nContent-Type: ");
    } else {
      hlen = str_append(hdr, sizeof(hdr), hlen, "HTTP/1.0 200 OK\r\nContent-Type: ");
    }
    hlen = str_append(hdr, sizeof(hdr), hlen, get_mime_type(path));
    hlen = str_append(hdr, sizeof(hdr), hlen, "\r\nContent-Length: ");
    hlen = int_append(hdr, sizeof(hdr), hlen, st.size);
    if (is_http11) {
      hlen = str_append(hdr, sizeof(hdr), hlen, "\r\nConnection: ");
      hlen = str_append(hdr, sizeof(hdr), hlen, keep_alive ? "keep-alive" : "close");
    }
    hlen = str_append(hdr, sizeof(hdr), hlen, "\r\n\r\n");

    // gui toan bo header trong mot lan write duy nhat
    write(client_fd, hdr, hlen);

    // truyen du lieu noi dung file theo khoi 4KB
    if (!is_head) {
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
    }

    close(file_fd);
  }

  close(client_fd);
}

// vong lap phuc vu cua tung worker process
static void
worker_loop(int listen_fd)
{
  while (1) {
    int client_fd = accept(listen_fd);
    if (client_fd < 0)
      continue;
    handle_http_client(client_fd);
  }
}

int
main(int argc, char *argv[])
{
  int port = HTTP_PORT;
  if (argc > 1)
    port = atoi(argv[1]);

  printf("httpd: port %d\n", port);

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

  if (listen(listen_fd, BACKLOG) < 0) {
    printf("httpd: listen failed\n");
    close(listen_fd);
    exit(1);
  }

  // khoi tao pre-fork worker pool
  int workers_started = 0;
  for (int i = 0; i < NWORKERS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("httpd: fork failed\n");
      break;
    }
    if (pid == 0) {
      // tien trinh con: vao vong lap lang nghe va phuc vu
      worker_loop(listen_fd);
      exit(0);
    }
    workers_started++;
  }

  // neu khong the fork bat ky worker nao, tien trinh cha tu phuc vu
  if (workers_started == 0) {
    worker_loop(listen_fd);
    exit(0);
  }

  // tien trinh cha: giam sat va tu dong hoi sinh neu co worker bi loi
  while (1) {
    int status;
    int dead_pid = wait(&status);
    if (dead_pid > 0) {
      int new_pid = fork();
      if (new_pid == 0) {
        worker_loop(listen_fd);
        exit(0);
      }
    }
  }

  exit(0);
}
