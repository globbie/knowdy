#pragma once

/**
    HTTP codes 
    http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html#sec6.1.1
*/
typedef enum knd_http_code_t {
  HTTP_CONTINUE = 100,                         // Section 10.1.1: Continue
  HTTP_SWITCHING_PROTOCOLS = 101,              // Section 10.1.2: Switching Protocols

  HTTP_OK = 200,                               // Section 10.2.1: OK
  HTTP_CREATED = 201,                          // Section 10.2.2: Created
  HTTP_ACCEPTED =  202,                        // Section 10.2.3: Accepted
  HTTP_NON_AUTHORITATIVE_INFORMATION = 203,    // Section 10.2.4: Non-Authoritative Information
  HTTP_NO_CONTENT =  204,                      // Section 10.2.5: No Content
  HTTP_RESET_CONTENT = 205,                    // Section 10.2.6: Reset Content
  HTTP_PARTIAL_CONTENT = 206,                  // Section 10.2.7: Partial Content

  HTTP_MULTIPLE_CHOICES = 300,                 // Section 10.3.1: Multiple Choices
  HTTP_MOVED_PERMANENTLY =  301,               // Section 10.3.2: Moved Permanently
  HTTP_FOUND =  302,                           // Section 10.3.3: Found
  HTTP_SEE_OTHER = 303,                        // Section 10.3.4: See Other
  HTTP_NOT_MODIFIED =  304,                    // Section 10.3.5: Not Modified
  HTTP_USE_PROXY =  305,                       // Section 10.3.6: Use Proxy
  HTTP_TEMPORARY_REDIRECT = 307,               // Section 10.3.8: Temporary Redirect

  HTTP_BAD_REQUEST = 400,                      // Section 10.4.1: Bad Request
  HTTP_UNAUTHORIZED = 401,                     // Section 10.4.2: Unauthorized
  HTTP_PAYMENT_REQUIRED =  402,                // Section 10.4.3: Payment Required
  HTTP_FORBIDDEN = 403,                        // Section 10.4.4: Forbidden
  HTTP_NOT_FOUND = 404,                        // Section 10.4.5: Not Found
  HTTP_METHOD_NOT_ALLOWED = 405,               // Section 10.4.6: Method Not Allowed
  HTTP_NOT_ACCEPTABLE = 406,                   // Section 10.4.7: Not Acceptable
  HTTP_PROXY_AUTHENTICATION_REQUIRED = 407,    // Section 10.4.8: Proxy Authentication Required
  HTTP_REQUEST_TIMEOUT = 408,                  // Section 10.4.9: Request Time-out
  HTTP_CONFLICT = 409,                         // Section 10.4.10: Conflict
  HTTP_GONE =  410,                            // Section 10.4.11: Gone
  HTTP_LENGTH_REQUIRED = 411,                  // Section 10.4.12: Length Required
  HTTP_PRECONDITION_FAILED =  412,             // Section 10.4.13: Precondition Failed
  HTTP_REQUEST_ENTITY_TOO_LARGE =  413,        // Section 10.4.14: Request Entity Too Large
  HTTP_REQUEST_URI_TOO_LARGE =  414,           // Section 10.4.15: Request-URI Too Large
  HTTP_UNSUPPORTED_MEDIA_TYPE =  415,          // Section 10.4.16: Unsupported Media Type
  HTTP_REQUESTED_RANGE_NOT_SATISFIABLE = 416,  // Section 10.4.17: Requested range not satisfiable
  HTTP_EXPECTATION_FAILED = 417,               // Section 10.4.18: Expectation Failed

  HTTP_INTERNAL_SERVER_ERROR =  500,           // Section 10.5.1: Internal Server Error
  HTTP_NOT_IMPLEMENTED = 501,                  // Section 10.5.2: Not Implemented
  HTTP_BAD_GATEWAY = 502,                      // Section 10.5.3: Bad Gateway
  HTTP_SERVICE_UNAVAILABLE =  503,             // Section 10.5.4: Service Unavailable
  HTTP_GATEWAY_TIMEOUT =  504,                 // Section 10.5.5: Gateway Time-out
  HTTP_HTTP_VERSION_NOT_SUPPORTED = 505        // Section 10.5.6: HTTP Version not supported
} knd_http_code_t;
