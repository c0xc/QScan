#!/usr/bin/env python3
"""
eSCL Scanner Compatibility Detector

Detects if a network scanner supports working eSCL pull scanning.
Distinguishes between true eSCL devices and SOAP-HT (HorseThief) devices.

Reference: M476DW_ESCL_INCOMPATIBILITY_ANALYSIS.md
"""

import socket
import ssl
import sys
import urllib.request
import urllib.error
from typing import Tuple, Optional

SOAP_HT_PORT = 8289
ESCL_HTTPS_PORT = 443
ESCL_HTTP_PORT = 80
TIMEOUT = 2.0


def check_tcp_port(host: str, port: int, timeout: float = TIMEOUT) -> bool:
    """Check if a TCP port is open on the host."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((host, port))
        sock.close()
        return result == 0
    except socket.error:
        return False


def get_scanner_capabilities(host: str, timeout: float = TIMEOUT) -> Tuple[Optional[str], Optional[str]]:
    """
    Query eSCL ScannerCapabilities endpoint.
    Returns (server_header, response_body) or (None, None) on failure.
    """
    urls = [
        f"https://{host}/eSCL/ScannerCapabilities",
        f"http://{host}/eSCL/ScannerCapabilities",
    ]

    for url in urls:
        try:
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE

            req = urllib.request.Request(url, headers={"Accept": "application/xml"})
            
            if url.startswith("https"):
                opener = urllib.request.build_opener(
                    urllib.request.HTTPSHandler(context=ctx)
                )
            else:
                opener = urllib.request.build_opener()
            
            opener.addheaders = [("Accept", "application/xml")]
            response = opener.open(req, timeout=timeout)
            
            server = response.getheader("Server")
            body = response.read(4096).decode("utf-8", errors="ignore")
            return server, body
            
        except (urllib.error.URLError, urllib.error.HTTPError, socket.timeout, OSError):
            continue
    
    return None, None


def has_working_escl(host: str, verbose: bool = False) -> bool:
    """
    Determine if a network scanner supports working eSCL pull scanning.
    
    Algorithm:
    1. Check TCP port 8289 - if open, device is SOAP-HT (not eSCL)
    2. Query /eSCL/ScannerCapabilities - if fails, not an eSCL device
    3. Check Server header - if contains "gSOAP", device is SOAP-HT
    
    Returns True only if device appears to be a working eSCL scanner.
    """
    if verbose:
        print(f"Checking {host}...")

    # Step 1: Check for SOAP-HT port
    if check_tcp_port(host, SOAP_HT_PORT):
        if verbose:
            print(f"  Port {SOAP_HT_PORT} open -> SOAP-HT device (not eSCL)")
        return False
    elif verbose:
        print(f"  Port {SOAP_HT_PORT} closed -> continuing")

    # Step 2: Query ScannerCapabilities
    server, body = get_scanner_capabilities(host)
    
    if server is None:
        if verbose:
            print("  ScannerCapabilities failed -> not an eSCL device")
        return False
    
    if verbose:
        print(f"  Server header: {server}")

    # Step 3: Check for gSOAP (SOAP-HT indicator)
    if server and "gSOAP" in server:
        if verbose:
            print("  Server contains 'gSOAP' -> SOAP-HT device (not eSCL)")
        return False

    if verbose:
        print("  -> Working eSCL device detected")
    return True


def detect_scanner(host: str, verbose: bool = False) -> dict:
    """
    Full scanner detection with detailed status.
    
    Returns dict with:
        - compatible: bool - device supports working eSCL
        - protocol: str - detected protocol ('escl', 'soap-ht', 'unknown')
        - port_8289_open: bool
        - server_header: str or None
    """
    result = {
        "compatible": False,
        "protocol": "unknown",
        "port_8289_open": False,
        "server_header": None,
    }

    result["port_8289_open"] = check_tcp_port(host, SOAP_HT_PORT)
    
    if result["port_8289_open"]:
        result["protocol"] = "soap-ht"
        if verbose:
            print(f"{host}: SOAP-HT device (port {SOAP_HT_PORT} open) - not eSCL compatible")
        return result

    server, body = get_scanner_capabilities(host)
    result["server_header"] = server

    if server is None:
        if verbose:
            print(f"{host}: ScannerCapabilities failed - unknown protocol")
        return result

    if "gSOAP" in server:
        result["protocol"] = "soap-ht"
        if verbose:
            print(f"{host}: SOAP-HT device (gSOAP detected) - not eSCL compatible")
        return result

    result["compatible"] = True
    result["protocol"] = "escl"
    if verbose:
        print(f"{host}: eSCL compatible (Server: {server})")
    
    return result


def main():
    if len(sys.argv) < 2:
        print("Usage: detect_escl_scanner.py <host> [--verbose]")
        print("       detect_escl_scanner.py --test")
        sys.exit(1)

    if sys.argv[1] == "--test":
        print("Running self-test with known devices...")
        print()
        
        #good_scanner = "1..."
        #bad_scanner = "10..."
        
        #print(f"Testing good scanner: {good_scanner}")
        #result_good = detect_scanner(good_scanner, verbose=True)
        #print(f"  Result: {result_good}")
        #print()
        
        #print(f"Testing bad scanner: {bad_scanner}")
        #result_bad = detect_scanner(bad_scanner, verbose=True)
        #print(f"  Result: {result_bad}")
        #print()
        
        #print("Test complete.")
        return

    host = sys.argv[1]
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    
    result = detect_scanner(host, verbose=verbose)
    
    if result["compatible"]:
        print(f"{host}: COMPATIBLE (eSCL)")
        sys.exit(0)
    else:
        protocol = result["protocol"]
        if protocol == "soap-ht":
            print(f"{host}: INCOMPATIBLE (SOAP-HT - use hplip backend)")
        elif protocol == "unknown":
            print(f"{host}: UNKNOWN (ScannerCapabilities not available)")
        else:
            print(f"{host}: INCOMPATIBLE")
        sys.exit(1)


if __name__ == "__main__":
    main()
