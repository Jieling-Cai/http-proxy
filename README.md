# Http Proxy
This is a simple version of HTTP proxy supported the following functionalities:
(For a much more complex and comprehensive version of an HTTP proxy, pls reference "CS-112-Final-Project" repository)

```bash
(1) Listen on a port for client connection requests.
(2) Handle GET requests one client at a time
(3) Caching: Evict stale items first, then LRU items. 
```
## Usage
```bash
make
./a.out [port]
```
