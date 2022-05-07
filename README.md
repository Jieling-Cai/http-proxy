# HTTP Proxy
This is a simple version of HTTP proxy supported the following functionalities:
(For a much more complex and comprehensive version of a proxy server, pls reference "CS-112-Final-Project" repository)

```bash
(1) Listen on a port for client connection requests.
(2) Handle GET requests one client at a time.
(3) Caching (for network reducing latency): When the cache is full, the proxy evicts stale items first and then LRU items.  
```
## Usage
```bash
make
./a.out [port]
```
