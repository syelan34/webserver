#pragma once

#define STREAM_CHUNK_SIZE 512

// supported header types
enum HTTP_HEADER {
    HTTP_GH_DATE = 0,
    HTTP_GH_PRAGMA,
    HTTP_GH_CONNECTION,
    HTTP_EH_ALLOW,
    HTTP_EH_CONTENT_ENCODING,
    HTTP_EH_CONTENT_LENGTH,
    HTTP_EH_CONTENT_TYPE,
    HTTP_EH_EXPIRES,
    HTTP_EH_LASTMODIFIED,
    HTTP_RQH_AUTHORIZATION,
    HTTP_RQH_FROM,
    HTTP_RQH_IFMODIFIEDSINCE,
    HTTP_RQH_REFERER,
    HTTP_RQH_USERAGENT,
    HTTP_RSH_LOCATION,
    HTTP_RSH_SERVER,
    HTTP_RSH_WWW_AUTHENTICATE,
    HTTP_H_UNKNOWN,
};

enum HTTP_METHOD {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_UNKNOWN
};

enum HTTP_MSGTYPE {
    HTTP_MSGTYPE_UNKNOWN = 0,
    HTTP_MSGTYPE_REQUEST,
    HTTP_MSGTYPE_RESPONSE
};

struct http_request_line {
    enum HTTP_METHOD method;
    const char* URI;
    const char* httpversion; // optional in case of simple request
};

struct http_response_line {
    const char* httpversion;
    int statuscode;
    const char* reasonphrase;
};

struct http_message {
    enum HTTP_MSGTYPE type;
    union {
        struct http_request_line requestline;
        struct http_response_line responseline;
    };
    const char* header_values[HTTP_H_UNKNOWN]; // last item in enum -> number of options
    void* entity;
};

// generates http request line struct from request line string.
int http_request_line_fromline(struct http_request_line* rqline, char* line);
// generates http message struct from buffer, returns size in bytes of message. After calling, buffer data is invalidated until the end of the message.
int http_message_frombuffer(struct http_message* rq, char* buf);
//  http header value from line
int http_header_setfromline(struct http_message *m, char* line);
//
int http_recv_request(struct http_message* rq, const char* recvbuf, int sockethandle);
// writes stringified http response struct to buf and returns size of string
int http_response_line_tostring(char* str, const struct http_response_line* r);
// creates http response string from http response struct, returns length of written string
int http_message_tostring(char* str, const struct http_message* m);
// int http_response_tostring(char* str, const struct http_response* r);

// creates http header string from http response struct, returns length of written string
// int http_header_tostring(char* str, );

int http_sendfile(const char* path, char* sendbuffer, int socket_handle);
int http_sendfilestreamed(const char* path, int socket_handle);

int http_send404(int socket_handle);

int http_init(char* recvbuffer, char* sendbuffer, int recvbuffersize, int sendbuffersize);

const char* http_get_filename_ext(const char *filename);

int http_message_setheader(struct http_message* msg, enum HTTP_HEADER header, const char* value);