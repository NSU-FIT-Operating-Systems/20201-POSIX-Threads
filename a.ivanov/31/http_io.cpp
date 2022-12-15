#include "http_io.h"

#include "httpparser/src/httpparser/httpresponseparser.h"
#include "httpparser/src/httpparser/response.h"

#include "io_operations.h"

using namespace httpparser;

namespace http_io {

    /*
     *
     */
    HttpResponseParser::ParseResult read_from_server(const io_operations::message &current_message,
                                                     Response *response);


}
