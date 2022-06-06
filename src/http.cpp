#include "http.hpp"

#include <utility>

namespace http {
request::request(EMethod method, std::string url)
    : m_url(std::move(url)) {
    switch (method) {
        case REQUEST_GET:
            m_method = "GET";
            break;
        case REQUEST_POST:
            m_method = "POST";
            break;
        case REQUEST_PATCH:
            m_method = "PATCH";
            break;
        case REQUEST_PUT:
            m_method = "PUT";
            break;
        case REQUEST_DELETE:
            m_method = "DELETE";
            break;
        default:
            m_method = "GET";
            break;
    }

    prepare();
}

request::request(request &&other) noexcept
    : m_curl(std::exchange(other.m_curl, nullptr))
    , m_url(std::exchange(other.m_url, ""))
    , m_method(std::exchange(other.m_method, nullptr))
    , m_header_list(std::exchange(other.m_header_list, nullptr))
    , m_error_buf(other.m_error_buf)
    , m_form(std::exchange(other.m_form, nullptr)) {
    // i think this is correct???
}

request::~request() {
    if (m_curl != nullptr)
        curl_easy_cleanup(m_curl);

    if (m_header_list != nullptr)
        curl_slist_free_all(m_header_list);

    if (m_form != nullptr)
        curl_mime_free(m_form);
}

const std::string &request::get_url() const {
    return m_url;
}

const char *request::get_method() const {
    return m_method;
}

void request::set_verify_ssl(bool verify) {
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
}

void request::set_proxy(const std::string &proxy) {
    curl_easy_setopt(m_curl, CURLOPT_PROXY, proxy.c_str());
}

void request::set_header(const std::string &name, const std::string &value) {
    m_header_list = curl_slist_append(m_header_list, (name + ": " + value).c_str());
}

void request::set_body(const std::string &data) {
    curl_easy_setopt(m_curl, CURLOPT_COPYPOSTFIELDS, data.c_str());
}

void request::set_user_agent(const std::string &data) {
    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, data.c_str());
}

void request::make_form() {
    m_form = curl_mime_init(m_curl);
}

// file must exist until request completes
void request::add_file(std::string_view name, std::string_view file_path, std::string_view filename) {
    auto *field = curl_mime_addpart(m_form);
    curl_mime_name(field, name.data());
    curl_mime_filedata(field, file_path.data());
    curl_mime_filename(field, filename.data());
}

// copied
void request::add_field(std::string_view name, const char *data, size_t size) {
    puts(name.data());
    auto *field = curl_mime_addpart(m_form);
    curl_mime_name(field, name.data());
    curl_mime_data(field, data, size);
}

response request::execute() {
    if (m_curl == nullptr) {
        auto response = detail::make_response(m_url, EStatusCode::ClientErrorCURLInit);
        response.error_string = "curl pointer is null";
    }

    detail::check_init();

    std::string str;
    curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, m_method);
    curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, detail::curl_write_data_callback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &str);
    curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, m_error_buf);
    m_error_buf[0] = '\0';
    if (m_header_list != nullptr)
        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_header_list);
    if (m_form != nullptr)
        curl_easy_setopt(m_curl, CURLOPT_MIMEPOST, m_form);

    CURLcode result = curl_easy_perform(m_curl);
    if (result != CURLE_OK) {
        auto response = detail::make_response(m_url, EStatusCode::ClientErrorCURLPerform);
        response.error_string = curl_easy_strerror(result);
        response.error_string += " " + std::string(m_error_buf.data());
        return response;
    }

    long response_code = 0;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response_code);

    auto response = detail::make_response(m_url, response_code);
    response.text = str;

    return response;
}

void request::prepare() {
    m_curl = curl_easy_init();
}

namespace detail {
    size_t curl_write_data_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
        const size_t n = size * nmemb;
        static_cast<std::string *>(userdata)->append(static_cast<char *>(ptr), n);
        return n;
    }

    response make_response(const std::string &url, int code) {
        response r;
        r.url = url;
        r.status_code = static_cast<EStatusCode>(code);
        if (code < http::EStatusCode::ClientErrorMax)
            r.error = true;
        return r;
    }

    void check_init() {
        static bool initialized = false;
        if (!initialized) {
            curl_global_init(CURL_GLOBAL_ALL);
            initialized = true;
        }
    }
} // namespace detail
} // namespace http
