# ipmond
Monitoring network interface state and address changes

```
pandax381@A285:~/LOCAL/src/ipmond$ ./ipmond -D
DEBUG: 1 (lo) Link is UP
DEBUG: 2 (enp4s0f0) Link is UP
DEBUG: 3 (wlp2s0) Link is UP
DEBUG: 1 (lo): NEWADDR 127.0.0.1/8
DEBUG: 2 (enp4s0f0): NEWADDR 10.15.2.165/24
2 (enp4s0f0): Link is DOWN
2 (enp4s0f0): DELADDR 10.15.2.165/24
DEBUG: 2 (enp4s0f0): Interface has no address
2 (enp4s0f0): Link is UP
2 (enp4s0f0): NEWADDR 10.15.2.165/24
DEBUG: 32 (eth0): Detect New LINK
DEBUG: 32 (eth0): Interface name has been changed to 'enx001f5bfeefcd'
32 (enx001f5bfeefcd): Link is UP
32 (enx001f5bfeefcd): NEWADDR 192.168.100.101/24
DEBUG: 32 (enx001f5bfeefcd): Address already exists '192.168.100.101/24'
DEBUG: 32 (enx001f5bfeefcd): Address already exists '192.168.100.101/24'
```
