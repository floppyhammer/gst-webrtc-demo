## FEC ❌ | Audio ❌

### H264

PC->PC 50ms

PC->Mobile (Wi-Fi) 170ms

Mobile->Mobile (Wi-Fi) ?

Mobile->Mobile (Wi-Fi P2P) ?

### VP8

PC->PC (localhost) 34ms

PC->Mobile (Wi-Fi) 160ms

PC->Mobile (Wi-Fi, glsinkbin sync=false) 100ms

Mobile->Mobile (Wi-Fi) ?

Mobile->Mobile (Wi-Fi P2P) ?

## FEC ❌ | Audio ✅

### H264

PC->PC (localhost) 150ms

PC->Mobile (Wi-Fi) 150ms

### VP8

PC->PC (localhost) 150ms

PC->Mobile (Wi-Fi) 300ms

## Notes

- Enabling/Disabling FEC doesn't affect latency if there's no packet loss.
