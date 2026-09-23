#include "include/configs/sub/clash.hpp"

#include <cassert>
#include <string>

int main() {
    const std::string yaml = R"yaml(
host: front.synthetic.example
path: /edge
mode: packet-up
headers:
  X-Synthetic: fixture
x-padding-bytes: 100-200
xPaddingObfsMode: true
xPaddingKey: pad
uplink-http-method: PUT
uplinkHTTPMethod: GET
sessionPlacement: path
sessionIDPlacement: header
sessionIDKey: sid
sessionIDTable: abc
sessionIDLength: 16-24
seqPlacement: query
seqKey: seq
uplinkDataPlacement: cookie
uplinkDataKey: data
uplinkChunkSize: 4096
noGRPCHeader: true
no-sse-header: true
scMaxEachPostBytes: 1000000
sc-min-posts-interval-ms: 20-30
scMaxBufferedPosts: 8
scStreamUpServerSecs: 30-60
serverMaxHeaderBytes: 8192
reuse-settings:
  max-concurrency: 4-8
  maxConnections: 2
download-settings:
  server: download.synthetic.example
  port: 443
  tls: true
  mode: auto
  uplinkHTTPMethod: POST
  sessionIDPlacement: path
  noSSEHeader: true
)yaml";

    const auto node = fkyaml::node::deserialize(yaml);
    const auto opts = node.get_value<clash::xhttpOpts>();

    assert(opts.host == "front.synthetic.example");
    assert(opts.path == "/edge");
    assert(opts.mode == "packet-up");
    assert(opts.headers.at("X-Synthetic") == "fixture");
    assert(opts.x_padding_bytes == "100-200");
    assert(bool(opts.x_padding_obfs_mode));
    assert(opts.x_padding_key == "pad");
    // The canonical camelCase spelling has the highest precedence.
    assert(opts.uplink_http_method == "GET");
    assert(opts.session_id_placement == "header");
    assert(opts.session_id_key == "sid");
    assert(opts.session_id_table == "abc");
    assert(opts.session_id_length == "16-24");
    assert(opts.seq_placement == "query");
    assert(opts.seq_key == "seq");
    assert(opts.uplink_data_placement == "cookie");
    assert(opts.uplink_data_key == "data");
    assert(opts.uplink_chunk_size == "4096");
    assert(bool(opts.no_grpc_header));
    assert(bool(opts.no_sse_header));
    assert(opts.sc_max_each_post_bytes == "1000000");
    assert(opts.sc_min_posts_interval_ms == "20-30");
    assert(opts.sc_max_buffered_posts == "8");
    assert(opts.sc_stream_up_server_secs == "30-60");
    assert(int(opts.server_max_header_bytes) == 8192);
    assert(opts.reuse_settings.max_concurrency == "4-8");
    assert(opts.reuse_settings.max_connections == "2");
    assert(opts.has_download_settings);
    assert(opts.download_settings.server == "download.synthetic.example");
    assert(opts.download_settings.uplink_http_method == "POST");
    assert(opts.download_settings.session_id_placement == "path");
    assert(bool(opts.download_settings.no_sse_header));
    return 0;
}

