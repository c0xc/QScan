# HP Color LaserJet MFP M476dw eSCL Compatibility Analysis

The HP Color LaserJet MFP M476dw reports eSCL capability but does not support eSCL scanning. The device uses the SOAP-HT protocol on port 8289 instead.

**Test devices:**
- Bad (SOAP-HT): HP Color LaserJet MFP M476dw
- Good (eSCL): HP LaserJet Pro MFP 3102fdw

---

## Observed Behavior

All ScanJobs POST requests fail with HTTP 400 and empty body. ScannerCapabilities and ScannerStatus endpoints return valid responses.

**Note:** SOAP-HT devices respond to eSCL GET endpoints. ScannerCapabilities returns MakeAndModel and other device information. Other standard eSCL endpoints return HTTP error codes (400, 504).

---

## Protocol Mismatch

### Scanner Classification

The M476dw is classified in hplip with `scan-type=5`. From `hplip-3.25.8/data/models/models.dat`:

```ini
[hp_color_laserjet_mfp_m476dw]
scan-type=5
```

### Protocol Definition

Source: `hplip-3.25.8/io/hpmud/hpmud.h`
```c
enum HPMUD_SCANTYPE
{
   HPMUD_SCANTYPE_NA = 0,
   HPMUD_SCANTYPE_SCL = 1,
   HPMUD_SCANTYPE_PML = 2,
   HPMUD_SCANTYPE_SOAP = 3,
   HPMUD_SCANTYPE_MARVELL = 4,
   HPMUD_SCANTYPE_SOAPHT = 5,   /* HorseThief */
   HPMUD_SCANTYPE_SCL_DUPLEX = 6,
   HPMUD_SCANTYPE_LEDM = 7,
   HPMUD_SCANTYPE_MARVELL2 = 8,
   HPMUD_SCANTYPE_ESCL = 9,
   HPMUD_SCANTYPE_ORBLITE = 10
};
```

Value 5 corresponds to `HPMUD_SCANTYPE_SOAPHT`, not `HPMUD_SCANTYPE_ESCL` (value 9).

### Protocol Selection Logic

Source: `hplip-3.25.8/scan/sane/hpaio.c`
```c
if (ma.scantype == HPMUD_SCANTYPE_SOAPHT)
   return soapht_open(devicename, pHandle);
if (ma.scantype == HPMUD_SCANTYPE_ESCL)
   return escl_open(devicename, pHandle);
```

Protocols are mutually exclusive based on scan-type.

### Port Assignment

Source: `hplip-3.25.8/io/hpmud/jd.c`
```c
case HPMUD_SOAPSCAN_CHANNEL:
   port = 8289;
```

Port 8289 is hardcoded for SOAP-HT. eSCL uses HTTP (80/443).

---

## Detection Algorithm Specification

### Objective

Determine if a network scanner supports working eSCL pull scanning without triggering a scan job.

### Constraints

1. Must not POST to /eSCL/ScanJobs
2. Must work standalone without hplip dependency
3. Must handle both SOAP-HT and eSCL devices (apparently mutually exclusive)

### Algorithm

```
Function: hasWorkingESCL(host: string) -> bool

Step 1: Check TCP port 8289
    Attempt TCP connection to host:8289 with timeout (2 seconds)
    
    If connection succeeds:
        Return false (SOAP-HT device)
    
    If connection fails:
        Continue to Step 2

Step 2: Query eSCL ScannerCapabilities
    GET https://host/eSCL/ScannerCapabilities
    
    If response empty or fails:
        Return false (not eSCL device)
    
    Extract "Server" HTTP response header

Step 3: Check server header
    If server contains "gSOAP":
        Return false (SOAP-HT device)
    
    If response succeeds and server is not gSOAP:
        Return true (assume eSCL working)
```
