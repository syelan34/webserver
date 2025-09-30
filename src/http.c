#include "http.h"
#include "datetime.h"
#include "mime.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <ctype.h>
// #include <assert.h>

const char* http_method_strings[] = {
    "GET",
    "HEAD",
    "POST"
};

const char* http_header_strings[] = {
    "Date", 
    "Pragma", 
    "Connection",
    "Allow", 
    "Content-Encoding", 
    "Content-Length", 
    "Content-Type", 
    "Expires", 
    "Last-Modified",
    "Authorization",
    "From",
    "If-Modified-Since",
    "Referer",
    "User-Agent",
    "Location",
    "Server",
    "WWW_Authenticate"
};

const char* http_get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot;
}

char* http_trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}

int http_header_tostring(char* buf, const enum HTTP_HEADER header, const char* value) {
    int len = 1;
    if (!value) return len;
    len += strlen(http_header_strings[header]);
    len += 1; // for the required : character
    len += strlen(value);
    len += 2; // for the CRLF
    
    if (buf) {
        memset(buf, 0, len * sizeof(char));
        strcpy(buf, http_header_strings[header]);
        strcat(buf, ":");
        strcat(buf, value);
        strcat(buf, "\r\n");
        
    }
    
    return len;
}

int http_message_headers_tostring(char* str, const struct http_message* msg) {
    int len = 0;
    for (int i = 0; i < HTTP_H_UNKNOWN; i++) {
        len += http_header_tostring(str ? str + len : NULL, i, msg->header_values[i]) - 1;
    }
    return len + 1; // for null terminator
}

// only checks whether it's a request or response. Doesn't check for well-formedness.
const enum HTTP_MSGTYPE http_check_request_or_response(const char* msg) {
    // if it starts with the HTTP version then it's a response (for HTTP/1.0 and above)
    // if using 0.9, the only possible request is GET so if it's not that it must be a response, even though it doesn't start with the HTTP version
    return (strncmp(msg, "HTTP/", 5) == 0 || strncmp(msg, "GET", 3) != 0) ? HTTP_MSGTYPE_RESPONSE : HTTP_MSGTYPE_REQUEST;
}

// static_assert(http_check_request_or_response("GET / HTTP/1.0\r\n") == HTTP_MSGTYPE_REQUEST); // Request line valid for any HTTP version
// static_assert(http_check_request_or_response("POST /file.txt HTTP/1.0\r\n") == HTTP_MSGTYPE_REQUEST); // Request line valid for any HTTP version
// static_assert(http_check_request_or_response("HTTP/1.0 200 Ok\r\n") == HTTP_MSGTYPE_RESPONSE); // HTTP/1.0+ response line
// static_assert(http_check_request_or_response("200 Ok\r\n") == HTTP_MSGTYPE_RESPONSE); // HTTP/0.9 response line

int sendall(int s, char *buf, size_t *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n = 0;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n==-1?-1:0; // return -1 on failure, 0 on success
}

static void printchar(unsigned char theChar) {

    switch (theChar) {
        case '\n':
            printf("\\n");
            break;
        case '\r':
            printf("\\r");
            break;
        case '\t':
            printf("\\t");
            break;
        default:
            if ((theChar < 0x20) || (theChar > 0x7f)) {
                printf("\\%03o", (unsigned char)theChar);
            } else {
                printf("%c", theChar);
            }
        break;
   }
}

static void print_message_literal(const char* str, int len) {
    for (int i = 0; i < len; i++) {
        printchar(str[i]);
    }
}

int http_request_line_fromline(struct http_request_line* rqline, char* line) {
    char* methodname = strtok(line, " ");
    enum HTTP_METHOD method = -1;
    for (int i = 0; i < (sizeof(http_method_strings)/sizeof(http_method_strings[0])); i++) {
        if (strcmp(http_method_strings[i], methodname) == 0) method = i;
    }
    rqline->method = method;
    rqline->URI = strtok(NULL, " ");
    rqline->httpversion = strtok(NULL, " ");
    return 0;
}

int http_countstringtok(const char *buf, const char *tok){
    int count = 0;
    const char *tmp = buf;
    while((tmp = strstr(tmp, tok)))
    {
        count++;
        tmp++;
    }
    return count;
}

int http_message_frombuffer(struct http_message* m, char* buf) {
    // printf("Parsing message from text: \n");
    // print_message_literal(buf, strlen(buf));
    // printf("\n");
    char* header = buf; // entire beginning portion
    char* end = strstr(buf, "\r\n\r\n");
    *end = '\0'; // split out the string
    char* body = end + strlen("\r\n\r\n"); 
    // printf("header text: \n");
    // print_message_literal(buf, strlen(buf));
    // printf("\n");
    // char* body = buf + strlen(header) + 1; // entity body if there is one
    int numlines = http_countstringtok(header, "\r\n");
    // printf("number of lines in request: %d\n", numlines);
    char** lines = malloc(sizeof(char) * numlines);
    // extract lines of request ahead of usage since strtok is fucky
    lines[0] = strtok(header, "\r\n");
    for (int i = 1; i < numlines; i++) 
        lines[i] = strtok(NULL, "\r\t");
    
    switch (http_check_request_or_response(header)) {
        case HTTP_MSGTYPE_REQUEST: {
            http_request_line_fromline(&m->requestline, lines[0]);
            
            // the last header line is followed by 2 CRLF (so one empty line)
            
            for (int i = 1; i < numlines - 1; i++) {
                http_header_setfromline(m, http_trimwhitespace(lines[i]));
            }
            
            if (m->header_values[HTTP_EH_CONTENT_LENGTH] != NULL) {
                // we have an entity body
                m->entity = body;
            }
        }
        case HTTP_MSGTYPE_RESPONSE:
        case HTTP_MSGTYPE_UNKNOWN:
            break;
    }
    
    free(lines);
    return 0;
}

// not yet implemented
int http_recv_request(struct http_message* rq, const char* recvbuf, int sockethandle) {
    return 0;
}

int http_response_line_tostring(char* buf, const struct http_response_line* r) {
    int len = 0;
    // status-line format: HTTP-Version SP Status-Code SP Reason-Phrase CRLF
    len += strlen(r->httpversion);
    len += 5; // SP + 3 digit code + SP
    len += strlen(r->reasonphrase);
    len += 2; // for the CRLF
    len += 1; // for the null terminator
    
    
    if (buf) {
        memset(buf, 0, len); // clear buffer at least as much as we need
        strcat(buf, r->httpversion);
        strcat(buf, " ");
        char code[4] = {};
        snprintf(code, 4, "%d", r->statuscode);
        strcat(buf, code);
        strcat(buf, " ");
        strcat(buf, r->reasonphrase);
        strcat(buf, "\r\n");
    }
    
    return len;
}

int http_message_tostring(char* buf, const struct http_message* msg) {
    int len = 1; // default string is length 1 bc of null terminator
    switch (msg->type) {
        case HTTP_MSGTYPE_RESPONSE: {
            len += http_response_line_tostring(NULL, &msg->responseline); // only get length of response line
            len += http_message_headers_tostring(NULL, msg) - 1;
            int entitybodysize = 0;
            if (msg->entity) { // has entity body
                if (msg->header_values[HTTP_EH_CONTENT_LENGTH]) {
                    entitybodysize = atoi(msg->header_values[HTTP_EH_CONTENT_LENGTH]);
                }
            }
            len += entitybodysize;
            
            if (buf) {
                memset(buf, 0, len * sizeof(char));
                buf += http_response_line_tostring(buf, &msg->responseline) - 1;
                buf += http_message_headers_tostring(buf, msg) - 1;
                strcat(buf, "\r\n"); // CRLF required by RFC2616
                buf += 2; // advance
                memcpy(buf, msg->entity, entitybodysize * sizeof(char));
            }
            break;
        }
        // not handled
        case HTTP_MSGTYPE_REQUEST: 
        case HTTP_MSGTYPE_UNKNOWN:
            break;
    }
    
    
    return len;
}

int http_header_setfromline(struct http_message* message, char* line) {
    char* end = NULL;
    if ((end = strstr(line, "\r\n"))) *end = '\0';
    int len = strlen(line) + 1;
    char* header = line;
    end = strstr(line, ":");
    *end = '\0';
    char* token = end + 1;
    token = http_trimwhitespace(token);
    for (int i = 0; i < (sizeof(http_header_strings) / sizeof(http_header_strings[0])); i++) {
        if (strcmp(header, http_header_strings[i]) == 0) {
            int ret = http_message_setheader(message, i, token);
            if (ret != 0) return ret;
            return len;
        }
    }
    return -1; // couldn't find the header type
}

int http_message_setheader(struct http_message* msg, enum HTTP_HEADER header, const char* value) {
    if (!msg) return 1;
    if (header == HTTP_H_UNKNOWN) return 2;
    printf("Setting header '%s' to '%s'\n", http_header_strings[header], value);
    msg->header_values[header] = value;
    return 0;
}

int http_sendfile(const char* path, char* sendbuffer, int fd) {
    char* databuffer = NULL;
    FILE* f = fopen(path, "rb");
    if (!f) {
        // perror("Error opening file");
        printf("404 Not Found\n");
        return http_send404(fd);
    }
    
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f) + strlen(sendbuffer);
    if (len == -1) perror("Error seeking file");
    fseek(f, 0, SEEK_SET);
    databuffer = malloc(len * sizeof(char));
    size_t result = fread(databuffer, sizeof(char), len, f);
    if (result != len) perror("Error reading file");
    
    struct http_message response = {
        .type=HTTP_MSGTYPE_RESPONSE,
        .responseline = (struct http_response_line){
            .httpversion="HTTP/1.0",
            .statuscode=200,
            .reasonphrase="Ok"
        }
    };
    
    static char contentlengthstring[21];
    snprintf(contentlengthstring, 21, "%ld", len);
    
    char datebuffer[100] = {}, modificationdatebuffer[100] = {};
    time_t now;
    time(&now);
    UTCDateTimeString(datebuffer, sizeof(datebuffer), now);
    UTCDateTimeString(modificationdatebuffer, sizeof(modificationdatebuffer), FileModificationTime(path));
    
    
    http_message_setheader(&response, HTTP_EH_CONTENT_LENGTH, contentlengthstring);
    http_message_setheader(&response, HTTP_EH_CONTENT_TYPE, http_getMIMEtype(path));
    http_message_setheader(&response, HTTP_GH_DATE, datebuffer);
    http_message_setheader(&response, HTTP_RSH_SERVER, "my shitty ahh webserver");
    http_message_setheader(&response, HTTP_EH_LASTMODIFIED, modificationdatebuffer);
    
    response.entity = databuffer;
    
    size_t messagelen = http_message_tostring(sendbuffer, &response);
    
    printf("200 Ok\n");
    
    int ret = sendall(fd, sendbuffer, &messagelen);

    free(databuffer);
    return ret;
}

int http_sendfilestreamed(const char* path, int fd) {
    char sendbuffer[STREAM_CHUNK_SIZE];
    
    FILE* f = fopen(path, "rb");
    if (!f) {
        // perror("Error opening file");
        printf("404 Not Found\n");
        return http_send404(fd);
    }
    
    struct http_message response = {
        .type=HTTP_MSGTYPE_RESPONSE,
        .responseline = (struct http_response_line){
            .httpversion="HTTP/1.0",
            .statuscode=200,
            .reasonphrase="Ok"
        }
    };
    
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    if (len == -1) perror("Error seeking file");
    fseek(f, 0, SEEK_SET);
    
    static char contentlengthstring[21];
    snprintf(contentlengthstring, 21, "%ld", len);
    
    char datebuffer[100] = {}, modificationdatebuffer[100] = {};
    time_t now;
    time(&now);
    UTCDateTimeString(datebuffer, sizeof(datebuffer), now);
    UTCDateTimeString(modificationdatebuffer, sizeof(modificationdatebuffer), FileModificationTime(path));
    
    
    http_message_setheader(&response, HTTP_EH_CONTENT_LENGTH, contentlengthstring);
    http_message_setheader(&response, HTTP_EH_CONTENT_TYPE, http_getMIMEtype(path));
    http_message_setheader(&response, HTTP_GH_DATE, datebuffer);
    http_message_setheader(&response, HTTP_RSH_SERVER, "my silly webserver");
    http_message_setheader(&response, HTTP_EH_LASTMODIFIED, modificationdatebuffer);
    http_message_setheader(&response, HTTP_GH_CONNECTION, "keep-alive");
    
    size_t messagelen = http_message_tostring(sendbuffer, &response);
    
    printf("200 Ok\n");
    
    int ret = sendall(fd, sendbuffer, &messagelen); // send the starting bit
    
    int bytes_remaining = len;
    while (bytes_remaining > 0) {
        size_t len_to_send = bytes_remaining > STREAM_CHUNK_SIZE ? STREAM_CHUNK_SIZE : bytes_remaining;
        size_t result = fread(sendbuffer, sizeof(char), len_to_send, f);
        if (result != len_to_send) perror("Error reading file");
        
        sendall(fd, sendbuffer, &len_to_send);
        
        bytes_remaining -= len_to_send;
    }

    return ret;
}

int http_send404(int socket_handle) {
    size_t len = 26;
    return sendall(socket_handle, "HTTP/1.0 404 Not Found\r\n\r\n", &len);
}