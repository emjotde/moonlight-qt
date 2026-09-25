# Moonlight URI launching on Windows

This fork supports trusted dashboards, local HTML pages, and Windows shortcuts
that request a specific Moonlight host and application. The feature was added on
September 25, 2026 and is not part of the upstream Moonlight Qt v6.1.0 release.

## URI grammar

The only supported action is:

```text
moonlight://stream?<parameters>
```

`host` is required. At least one of `app` or `appId` is also required.
When both are present, Moonlight tries the stable application ID first and then
uses the application name as a fallback.

Example landscape and portrait links:

```text
moonlight://stream?host=host.example&app=Landscape%20Desktop&resolution=3840x2160&fps=60&displayMode=fullscreen
moonlight://stream?host=host.example&app=Portrait%20Desktop&resolution=2160x3840&fps=60&displayMode=fullscreen
```

Supported query parameters:

| Parameter | Required | Accepted values |
|---|---:|---|
| `host` | Yes | Known host UUID, hostname, IPv4 address, or IPv6 address |
| `app` | Unless `appId` is present | UTF-8 application name, percent-encoded in the URI |
| `appId` | No | Positive GameStream/Sunshine application ID |
| `resolution` | No | `WIDTHxHEIGHT`; each dimension must be 256-8192 |
| `fps` | No | Integer from 10-480 |
| `bitrate` | No | Integer from 500-500000 Kbps |
| `displayMode` | No | `fullscreen`, `borderless`, or `windowed` |
| `codec` | No | `auto`, `H.264`, `HEVC`, or `AV1` |
| `audioConfig` | No | `stereo`, `5.1-surround`, or `7.1-surround` |
| `quitAfter` | No | `true` or `false` |

Parameter names are case-sensitive. Enum values are case-insensitive. Unknown
actions and parameters, duplicate parameters, malformed percent escapes,
embedded NULs, invalid UTF-8, control characters, and out-of-range values are
rejected. Application names are percent-decoded exactly once.

## Launch precedence

Effective stream preferences are resolved in this order:

1. Explicit URI parameters
2. Explicit CLI stream parameters
3. The saved per-host/per-application profile
4. Global Moonlight settings

All effective preferences are session-owned. URI and CLI launches do not modify
global settings or save URI values into an application profile.

The normal CLI remains available:

```text
Moonlight.exe stream host.example "Portrait Desktop" --resolution 2160x3840 --fps 60
```

The protocol handler invokes:

```text
Moonlight.exe --uri "<URI>"
```

## Confirmation and trust

**Confirm external launch requests** is enabled by default in Moonlight's input
settings. Before a paired host is launched, Moonlight displays the resolved host,
application, resolution/FPS, and display mode. The choices are:

- **Launch once**
- **Always allow links for this host**
- **Cancel**

Permanent trust is stored against the paired host UUID, never its display name
or network address. Unpaired hosts use Moonlight's normal PIN pairing dialog.
Unknown, offline, unpaired/pairing-failed, app-not-found, and active-stream
conditions produce explicit UI errors.

Moonlight never sends URI fields to a shell, `cmd.exe`, PowerShell, or an
arbitrary executable. Parsed typed values are passed only through existing
Moonlight host, application, preference, and stream APIs.

## Single-instance behavior

An existing Moonlight process owns a local IPC endpoint scoped to its settings
store. A second `--uri` process forwards the complete URI, waits for a delivery
acknowledgment, and exits without creating another UI or session manager.

The IPC server runs on a dedicated event-loop thread, so Windows can deliver a
URI while SDL owns the main thread. Requests are queued until QML, preferences,
and host discovery are ready. Repeated identical URIs are suppressed. The
existing Moonlight window is restored and activated when a URI arrives.

## Windows registration

The Windows installer registers the scheme per-user under:

```text
HKEY_CURRENT_USER\Software\Classes\moonlight
```

The open command is derived from the installed executable:

```text
"<Moonlight.exe path>" --uri "%1"
```

The installer removes its registration during uninstall. Portable builds expose
these commands:

```text
Moonlight.exe --register-uri
Moonlight.exe --unregister-uri
```

Portable unregistration refuses to remove a registration owned by a different
Moonlight executable.
