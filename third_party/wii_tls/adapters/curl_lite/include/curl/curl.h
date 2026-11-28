/**
 * @file curl.h
 * @brief Optional libcurl-lite compatibility shim backed by libwiitls.
 *
 * Separate adapter library (wiitls_curl) — adds zero overhead to libwiitls.a.
 */
#ifndef WIITLS_CURL_H
#define WIITLS_CURL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void CURL;
typedef void CURLM;
typedef long long curl_off_t;

typedef enum {
    CURLE_OK = 0,
    CURLE_UNSUPPORTED_PROTOCOL = 1,
    CURLE_URL_MALFORMAT = 3,
    CURLE_COULDNT_RESOLVE_HOST = 6,
    CURLE_COULDNT_CONNECT = 7,
    CURLE_HTTP_RETURNED_ERROR = 22,
    CURLE_WRITE_ERROR = 23,
    CURLE_OUT_OF_MEMORY = 27,
    CURLE_OPERATION_TIMEDOUT = 28,
    CURLE_SSL_CONNECT_ERROR = 35,
    CURLE_ABORTED_BY_CALLBACK = 42,
    CURLE_BAD_FUNCTION_ARGUMENT = 43,
    CURLE_TOO_MANY_REDIRECTS = 47,
    CURLE_UNKNOWN_OPTION = 48,
    CURLE_READ_ERROR = 26,
    CURLE_RECV_ERROR = 56,
    CURLE_PEER_FAILED_VERIFICATION = 60,
} CURLcode;

typedef enum {
    CURLOPT_URL = 10002,
    CURLOPT_PROXY = 10004,
    CURLOPT_USERAGENT = 10018,
    CURLOPT_POSTFIELDS = 10015,
    CURLOPT_POSTFIELDSIZE = 60,
    CURLOPT_HTTPHEADER = 10023,
    CURLOPT_CUSTOMREQUEST = 10036,
    CURLOPT_WRITEFUNCTION = 20011,
    CURLOPT_WRITEDATA = 10001,
    CURLOPT_HEADERFUNCTION = 20079,
    CURLOPT_HEADERDATA = 10029,
    CURLOPT_NOBODY = 44,
    CURLOPT_FAILONERROR = 45,
    CURLOPT_POST = 47,
    CURLOPT_FOLLOWLOCATION = 52,
    CURLOPT_MAXREDIRS = 68,
    CURLOPT_SSL_VERIFYPEER = 64,
    CURLOPT_SSL_VERIFYHOST = 81,
    CURLOPT_HTTPGET = 80,
    CURLOPT_CONNECTTIMEOUT_MS = 156,
    CURLOPT_TIMEOUT_MS = 155,
    CURLOPT_ACCEPT_ENCODING = 10102,
    CURLOPT_USERPWD = 10005,
    CURLOPT_READDATA = 10009,
    CURLOPT_INFILESIZE = 14,
    CURLOPT_UPLOAD = 46,
    CURLOPT_READFUNCTION = 20012,
    CURLOPT_NOPROGRESS = 43,
    CURLOPT_XFERINFODATA = 10057,
    CURLOPT_INFILESIZE_LARGE = 30115,
    CURLOPT_POSTFIELDSIZE_LARGE = 30120,
    CURLOPT_XFERINFOFUNCTION = 20219,
} CURLoption;

typedef enum {
    CURLINFO_EFFECTIVE_URL = 0x100001,
    CURLINFO_RESPONSE_CODE = 0x200002,
    CURLINFO_CONTENT_TYPE = 0x100012,
    CURLINFO_REDIRECT_COUNT = 0x200014,
    CURLINFO_SIZE_DOWNLOAD_T = 0x600008,
} CURLINFO;

struct curl_slist {
    char *data;
    struct curl_slist *next;
};

typedef size_t (*curl_write_callback)(char *ptr, size_t size, size_t nmemb, void *userdata);

typedef size_t (*curl_read_callback)(char *buffer, size_t size, size_t nitems, void *userdata);
/* Return non-zero to abort the transfer (CURLE_ABORTED_BY_CALLBACK). */
typedef int (*curl_xferinfo_callback)(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                                      curl_off_t ulnow);

#define CURL_READFUNC_ABORT 0x10000000
#define CURL_GLOBAL_DEFAULT 0L

CURLcode curl_global_init(long flags);
void curl_global_cleanup(void);

CURL *curl_easy_init(void);
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
void curl_easy_cleanup(CURL *curl);
const char *curl_easy_strerror(CURLcode code);

/* ---- multi interface (sequential: see docs/curl-shim.md) ---- */

typedef enum {
    CURLM_OK = 0,
    CURLM_BAD_HANDLE = 2,
    CURLM_BAD_EASY_HANDLE = 3,
    CURLM_OUT_OF_MEMORY = 4,
    CURLM_BAD_FUNCTION_ARGUMENT = 10,
} CURLMcode;

typedef enum { CURLMSG_NONE = 0, CURLMSG_DONE = 1 } CURLMSG;

typedef struct {
    CURLMSG msg;
    CURL *easy_handle;
    union {
        void *whatever;
        CURLcode result;
    } data;
} CURLMsg;

struct curl_waitfd; /* not supported: pass NULL and 0 */

CURLM *curl_multi_init(void);
CURLMcode curl_multi_add_handle(CURLM *multi, CURL *easy);
CURLMcode curl_multi_remove_handle(CURLM *multi, CURL *easy);
CURLMcode curl_multi_perform(CURLM *multi, int *running_handles);
CURLMcode curl_multi_wait(CURLM *multi, struct curl_waitfd *extra_fds, unsigned extra_nfds, int timeout_ms,
                          int *numfds);
CURLMsg *curl_multi_info_read(CURLM *multi, int *msgs_in_queue);
CURLMcode curl_multi_cleanup(CURLM *multi);
const char *curl_multi_strerror(CURLMcode code);

struct curl_slist *curl_slist_append(struct curl_slist *list, const char *string);
void curl_slist_free_all(struct curl_slist *list);

#ifdef __cplusplus
}
#endif

#endif
