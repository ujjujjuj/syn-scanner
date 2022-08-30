# A TCP SYN Scanner

This is a simple implementation of a SYN scanner which scans for all open TCP ports of a machine. It uses raw sockets to send SYN packets and listens for SYN|ACK packets from the target server.

## Usage

Compile the code with
`gcc main.c -o synscanner`
then run it with
`./synscanner [target_host]`
where target host is the ip of the machine on which you want to perform the scan on.

Additionally, you can add the flag `--include-closed` to include closed ports in the program output. This is useful when you want to scan ports of a machine which don't have a service listening on them but are not blocked by a firewall.

## Note

This tool is just a proof of concept. Since there is no timing mechanism like nmap, it occasionally drops probes due to rate limiting which can give unreliable results.
