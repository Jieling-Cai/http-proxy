# Http Proxy
A simple version of HTTP proxy supported the following functionalities:

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
