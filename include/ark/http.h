#ifndef ARK_HTTP_H
#define ARK_HTTP_H

#include <stdio.h>
#include "shared.h"

#define HttpHost                          0
#define HttpCacheControl                  1
#define HttpConnection                    2
#define HttpAccept                        3
#define HttpAcceptLanguage                4
#define HttpAcceptEncoding                5
#define HttpUserAgent                     6
#define HttpReferer                       7
#define HttpXForwardedFor                 8
#define HttpOrigin                        9
#define HttpUpgradeInsecureRequests       10
#define HttpPragma                        11
#define HttpCookie                        12
#define HttpDnt                           13
#define HttpSecGpc                        14
#define HttpFrom                          15
#define HttpIfModifiedSince               16
#define HttpXRequestedWith                17
#define HttpXForwardedHost                18
#define HttpXForwardedProto               19
#define HttpXCsrfToken                    20
#define HttpSaveData                      21
#define HttpRange                         22
#define HttpContentLength                 23
#define HttpContentType                   24
#define HttpVary                          25
#define HttpDate                          26
#define HttpServer                        27
#define HttpExpires                       28
#define HttpContentEncoding               29
#define HttpLastModified                  30
#define HttpEtag                          31
#define HttpAllow                         32
#define HttpContentRange                  33
#define HttpAcceptCharset                 34
#define HttpAccessControlAllowCredentials 35
#define HttpAccessControlAllowHeaders     36
#define HttpAccessControlAllowMethods     37
#define HttpAccessControlAllowOrigin      38
#define HttpAccessControlMaxAge           39
#define HttpAccessControlMethod           40
#define HttpAccessControlRequestHeaders   41
#define HttpAccessControlRequestMethod    42
#define HttpAccessControlRequestMethods   43
#define HttpAge                           44
#define HttpAuthorization                 45
#define HttpContentBase                   46
#define HttpContentDescription            47
#define HttpContentDisposition            48
#define HttpContentLanguage               49
#define HttpContentLocation               50
#define HttpContentMd5                    51
#define HttpExpect                        52
#define HttpIfMatch                       53
#define HttpIfNoneMatch                   54
#define HttpIfRange                       55
#define HttpIfUnmodifiedSince             56
#define HttpKeepAlive                     57
#define HttpLink                          58
#define HttpLocation                      59
#define HttpMaxForwards                   60
#define HttpProxyAuthenticate             61
#define HttpProxyAuthorization            62
#define HttpProxyConnection               63
#define HttpPublic                        64
#define HttpRetryAfter                    65
#define HttpTe                            66
#define HttpTrailer                       67
#define HttpTransferEncoding              68
#define HttpUpgrade                       69
#define HttpWarning                       70
#define HttpWwwAuthenticate               71
#define HttpVia                           72
#define HttpStrictTransportSecurity       73
#define HttpXFrameOptions                 74
#define HttpXContentTypeOptions           75
#define HttpAltSvc                        76
#define HttpReferrerPolicy                77
#define HttpXXssProtection                78
#define HttpAcceptRanges                  79
#define HttpSetCookie                     80
#define HttpSecChUa                       81
#define HttpSecChUaMobile                 82
#define HttpSecFetchSite                  83
#define HttpSecFetchMode                  84
#define HttpSecFetchUser                  85
#define HttpSecFetchDest                  86
#define HttpCfRay                         87
#define HttpCfVisitor                     88
#define HttpCfConnectingIp                89
#define HttpCfIpcountry                   90
#define HttpSecChUaPlatform               91
#define HttpCdnLoop                       92
#define HttpHeadersMax                    93

struct HttpSlice {
  short start_offset;
  short end_offset;
};

struct HttpHeader {
  struct HttpSlice key;
  struct HttpSlice value;
};

typedef struct {
  boolean parsed;
  size_t cursor;
  size_t a;
  int status;
  unsigned char token;
  unsigned char type;
  unsigned char version;
  u64 method;
  struct HttpSlice tmp_key;
  struct HttpSlice uri;
  struct HttpSlice scratch;
  struct HttpSlice message;
  struct HttpSlice headers[HttpHeadersMax];
} ARKHttpMessage;

/* Parse HTTP message */
int ark_parse_http_request_message(ARKHttpMessage *http_msg, char *raw_http_msg, size_t raw_http_msg_length, size_t chunck_to_process_length);

#endif /* ARK_HTTP_H */
