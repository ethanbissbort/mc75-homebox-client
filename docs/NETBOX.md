# 🌐 NetBox Integration

> **Using a locally hosted NetBox as the second inventory backend**

---

## 📋 Table of Contents

- [Scope](#-scope)
- [Transport: HTTPS Does Not Work From This Device](#-transport-https-does-not-work-from-this-device)
- [Provisioning an API Token](#-provisioning-an-api-token)
- [Configuration](#-configuration)
- [The Scan Workflow](#-the-scan-workflow)
- [Offline Queue Behaviour](#-offline-queue-behaviour)
- [Troubleshooting](#-troubleshooting)

---

## 🎯 Scope

The client supports two inventory backends — HomeBox and NetBox — and **exactly
one is active at a time**. The active one is named by `activeBackend` in
`hb_conf.json`; the operator does not switch backends mid-scan, because a
dual-mode app where you have to remember which system you are in *while pulling
a trigger* is the reliability failure this design avoids.

### What the NetBox integration does

| Workflow | Requests | Notes |
|----------|----------|-------|
| **Look up a device from a scanned label** | 1 in the common case, 2 worst case | Asset tag first, then a `?q=` fallback |
| **Change a device's status** | 1 `PATCH` | Seven `DeviceStatusChoices` values |
| **Move a device** (site / location / rack / position / face) | 1 atomic `PATCH` | The whole positional set in one request |

### What it deliberately does not do

**No device creation.** Creating a NetBox device requires four mandatory
foreign keys — `device_type`, `role`, `site` and `status` — each of which needs
its own referential picker before you can even submit. That is four lookups and
four disambiguation screens on a 240x320 display with a numeric keypad, to
produce a record that still has to be finished at a desk. It is a web-UI job.
The handheld's value is at the rack, hands busy, answering "what is this box?"
and "it moved" — which is exactly the three workflows above.

**No consumables or spare-parts counting.** NetBox models infrastructure, not
stock levels. The client already has a backend that does consumables properly;
that is what HomeBox is for. Having two backends is the point.

**No cables.** `dcim/cables` carries no `asset_tag` and no `serial` — only a
free-text label — and its terminations are polymorphic generic relations whose
JSON shape varies by termination type. Disproportionate parsing cost for no
operational value on a handheld.

### Supported NetBox versions

**NetBox 3.6 through 4.6**, one code path. The only genuine break across that
range is that `device_role` was renamed `role` in NetBox 4.0, so the parser
reads `role` and falls back to `device_role`. The client never sends a
`version=` parameter in its `Accept` header — see
[406 Not Acceptable](#-406-not-acceptable-invalid-version-in-accept-header).

---

## 🔒 Transport: HTTPS Does Not Work From This Device

**Read this before you configure anything.** HTTPS from an MC75 to a modern
NetBox is not achievable. This is not a setting to find — it is a hard
cryptographic incompatibility, and there are three independent blockers, any
one of which alone is fatal.

### Blocker 1 — TLS 1.0 is the permanent ceiling

Windows Mobile 6.5 runs the Windows CE 5.2 kernel, and its Schannel supports
**SSL 2.0, SSL 3.0 and TLS 1.0 only**. There is no registry key: the documented
subkeys under `HKLM\Comm\SecurityProviders\SCHANNEL\Protocols\` are exactly
`Unified Hello`, `SSL2`, `SSL3` and `TLS 1.0`, and their `Enabled` value can
only *disable* a protocol that is already implemented. Creating a `TLS 1.1` key
does nothing — there is no code behind it.

There is no hotfix either. Microsoft did backport TLS 1.1/1.2, but only to
Windows Embedded Compact 7 and Compact 2013 — never to CE 5.x/6.0 or Windows
Mobile. And Zebra's own developer portal states that neither TLS 1.1 nor TLS
1.2 is supported by the Windows CE/Mobile schannel, and that a third-party
library would be required.

The apparent counter-example is not one: Zebra's Enterprise Browser advertises
TLS 1.2 on Windows Mobile devices, but that is Enterprise Browser bundling its
*own* TLS stack inside WebKit. It proves the OS stack can be bypassed, not that
the OS stack works.

No further OS images are coming — MC75 support was discontinued 2016-09-27 and
MC75A 2020-06-30.

### Blocker 2 — the cipher suite intersection is empty

Even if you force a server down to TLS 1.0, the set of suites this device can
offer and the set a stock modern OpenSSL 3 can offer is **the empty set**.

The device's complete inventory is RC4, DES, RC2 and Triple DES, with SHA-1 and
MD5 for hashing and RSA key transport (`PKCS`) only. **No AES, no SHA-256, no
DHE, no ECDHE, no ECDSA.** A real enumeration from a CE 6.0 device — a *newer*
kernel than WM6.5's — yields eight wire suites, the best of which is
`TLS_RSA_WITH_3DES_EDE_CBC_SHA`; everything else is RC4, single DES, or 40/56-bit
export grade.

Against that, an enumeration of the *entire* suite inventory of a stock Ubuntu
OpenSSL 3.0.13 with every restriction removed (`ALL:COMPLEMENTOFALL:@SECLEVEL=0`,
158 suites) returns **zero** RC4 suites, **zero** 3DES suites, **zero** DES
suites and **zero** export suites. `openssl ciphers -v 'DES-CBC3-SHA:@SECLEVEL=0'`
answers `no cipher match`. This is upstream behaviour, not a distro quirk: in
OpenSSL 3.0 the 3DES suites sit inside an `#ifndef OPENSSL_NO_WEAK_SSL_CIPHERS`
block and `no-weak-ssl-ciphers` is the build default; RC4 is likewise excluded
unless you configure `enable-weak-ssl-ciphers`.

The weakest RSA-key-exchange suites that exist at all are `AES128-SHA` /
`AES256-SHA` — and this device has no AES.

**So `ssl_protocols TLSv1; ssl_ciphers DEFAULT:@SECLEVEL=0;` is not sufficient.**
The handshake still dies with `no shared cipher`. Getting one common suite
requires recompiling OpenSSL with `enable-weak-ssl-ciphers` and relinking nginx
against it — not a config change.

### Blocker 3 — the certificate cannot be validated

WM5/6 cannot validate **SHA-256-signed** certificates, and every modern local-CA
tool — mkcert, step-ca, Caddy's internal CA, `openssl req` defaults — emits
SHA-256. Working around it means minting an RSA-2048, **SHA-1-signed** CA and
leaf specifically for this device, and SHA-1 collisions have been practical
since SHAttered in 2017, so that CA is genuinely forgeable rather than merely
deprecated.

Two further traps sit behind that:

- **Caddy cannot serve TLS 1.0 at any setting.** Its supported-protocol map
  contains only `tls1.2` and `tls1.3`; `tls1.0` appears in an
  `unsupportedProtocols` map used for logging, not enforcement. Caddy's internal
  CA also issues **ECDSA P-256** certificates, which this device's RSA-only key
  exchange could not use even over a working protocol.
- **Assume no SNI.** Schannel did not gain Server Name Indication until Vista.
  Any legacy endpoint must therefore be a dedicated IP:port, never a name-based
  virtual host — the device will always receive the default vhost's certificate.

### ✅ The configuration that works: scoped plain HTTP on a segmented LAN

Plain HTTP is a **documented, supported NetBox configuration**, and NetBox
forces nothing here. Every HTTPS-forcing knob is off by default:

| NetBox setting | Default |
|----------------|---------|
| `SECURE_SSL_REDIRECT` | `False` |
| `SESSION_COOKIE_SECURE` | `False` |
| `CSRF_COOKIE_SECURE` | `False` |
| `SECURE_HSTS_SECONDS` | `0` |
| `SECURE_HSTS_INCLUDE_SUBDOMAINS` | `False` |
| `SECURE_HSTS_PRELOAD` | `False` |

What actually forces HTTPS is the reference `contrib/nginx.conf`, which ships a
blanket `return 301 https://$host$request_uri;` on port 80. That redirect is the
thing to replace. Note also that gunicorn binds `127.0.0.1:8001`, so the device
cannot reach NetBox without a listener.

Keep 443 for humans and add a **scoped** HTTP listener for the scanners:

```nginx
server {
    listen 192.168.10.5:80;          # bind to the LAN interface only
    server_name netbox.example.com;

    allow 192.168.20.0/24;           # the scanner VLAN, and nothing else
    deny  all;

    client_max_body_size 25m;

    location /static/ { alias /opt/netbox/netbox/static/; }
    location / {
        proxy_pass http://127.0.0.1:8001;
        proxy_set_header X-Forwarded-Host  $http_host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Restrict it further to `location /api/` if the device only needs the API. In
`configuration.py`, confirm `ALLOWED_HOSTS` includes the exact IP or name the
MC75 will use — that is the one setting that is not already correct by default.

**The security lives at the link layer, and that is where it belongs:**

- **WPA2/WPA3 on the WiFi.** The device-to-AP hop is the actual exposure in a
  warehouse, WPA2-PSK/Enterprise already encrypts it, and it costs nothing. This
  is the single most valuable control.
- **A separate SSID and VLAN for scanners, with no route to the internet.**
- **A write-scoped NetBox API token** — only the object permissions these three
  workflows need, with an expiry and, if your version supports it, an
  `allowed_ips` restriction. Size the blast radius on the assumption that the
  token is compromised.

### About `allowInsecureTls`

The key exists, it is `false` unless you set it, and it deserves an honest
description.

When it is on, the client sets `INTERNET_FLAG_IGNORE_CERT_CN_INVALID` and
`INTERNET_FLAG_IGNORE_CERT_DATE_INVALID`, and retries with
`SECURITY_FLAG_IGNORE_UNKNOWN_CA` after an unknown-CA rejection. The effect is
that it **accepts any certificate from any party**. You keep encryption against
a passive eavesdropper and lose all authentication of the server. An active
on-path attacker — trivial on the wireless segment a warehouse scanner lives on
— terminates the connection and, because the API token rides in the
`Authorization` header, captures a working NetBox credential on the very first
request.

Combined with TLS 1.0 and 3DES (SWEET32, BEAST), that is not meaningfully
stronger than plain HTTP, and it is arguably *worse*, because the URL says
`https` and everyone stops thinking about it. It is defensible only on a trusted
isolated LAN where you already accept plain HTTP and simply prefer the traffic
to be opaque. `Controller::ConfigureNetbox` journals a line every time it is
enabled, so the downgrade lands in the audit trail rather than only in a config
file nobody re-reads.

**A reverse proxy downgraded to TLS 1.0 is not recommended.** It requires
building OpenSSL with `enable-weak-ssl-ciphers` and relinking nginx, minting a
SHA-1-signed private CA, provisioning that CA into the device ROOT store via a
`_setup.xml` CAB, and dedicating an IP:port because of the missing SNI. The
result is TLS 1.0 + 3DES-CBC on a forgeable SHA-1 chain, plus an unpatched
weak-cipher OpenSSL on your network — strictly more attack surface than plain
HTTP on a segmented VLAN, in exchange for the appearance of security.

If encryption on the wire is genuinely non-negotiable, the only real answer is
to replace the TLS stack: link wolfSSL or mbedTLS into the app and add a third
transport alongside the WinSock and WinInet paths. That bypasses Schannel
entirely and is the same approach Zebra took for Enterprise Browser. It is not
implemented here.

---

## 🔑 Provisioning an API Token

### Creating the token

In the NetBox UI: **user profile → API Tokens → Add a Token**. Give it the
narrowest object permissions the three workflows need, and set an expiry.

Two token attributes matter for a handheld:

- **`write_enabled`** (default `true`). Set it `false` for a scan-only,
  look-but-don't-touch deployment; writes then return 403 while reads keep
  working.
- **`allowed_ips`** — a client-IP allowlist. Useful, and a common source of
  surprise 403s behind NAT or a WiFi controller.

### Which scheme goes with which version

NetBox has two token generations, and `netboxAuthScheme` selects between them:

| Token type | `netboxAuthScheme` | Header sent | NetBox versions |
|------------|--------------------|-------------|-----------------|
| **v1 (legacy)** | `Token` | `Authorization: Token <40 hex chars>` | All 3.x and 4.x. **Deprecated in 4.6, removed in 5.0** |
| **v2** | `Bearer` | `Authorization: Bearer nbt_<key>.<secret>` | **NetBox 4.5+** |

A v2 token is recognisable by its `nbt_` prefix — the prefix exists to help
secret-scanning tools. v2 tokens are hashed server-side with a cryptographic
pepper, so the plaintext is shown once and cannot be recovered; save it when it
is displayed.

Sending the wrong scheme produces a 403 that looks exactly like a network fault,
so if reads fail immediately after a token change, check this first. Migrating
from v1 to v2 is this one config line — not a rebuild of an application whose
toolchain no longer exists.

---

## ⚙️ Configuration

`hb_conf.json` in the repository root is a documented template; the CAB deploys
it to `\Program Files\HBXClient\`. Seven keys drive the two-backend behaviour.
The full parameter table, including the HomeBox keys, is in
[DEPLOYMENT.md → Configuration Parameters](DEPLOYMENT.md#configuration-parameters).

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `activeBackend` | string | `hb` | Which backend takes new work. Must equal `homeboxInstanceId` or `netboxInstanceId` |
| `homeboxInstanceId` | string | `hb` | Instance id tagging HomeBox queue records |
| `netboxInstanceId` | string | `nb` | Instance id tagging NetBox queue records |
| `netboxBaseUrl` | string | `""` | NetBox server **root**, no `/api` suffix. Empty means NetBox is not configured |
| `netboxToken` | string | `""` | The API token, without the scheme word |
| `netboxAuthScheme` | string | `Token` | `Token` (v1) or `Bearer` (v2) |
| `allowInsecureTls` | bool | `false` | Accept any TLS certificate. See the transport section above |

### Worked example

A NetBox at `192.168.20.5` on the scanner VLAN, made the active backend, with a
v1 token:

```json
{
  "activeBackend": "nb-prod",
  "homeboxInstanceId": "hb",
  "apiBaseUrl": "http://homebox.example.lan:7745/api",
  "deviceId": "MC75-RACK-01",
  "apiKey": "",
  "authToken": "",
  "netboxInstanceId": "nb-prod",
  "netboxBaseUrl": "http://192.168.20.5",
  "netboxToken": "0123456789abcdef0123456789abcdef01234567",
  "netboxAuthScheme": "Token",
  "allowInsecureTls": false,
  "syncIntervalSeconds": 300,
  "journalPath": "\\My Documents\\hbx_journal.log",
  "logLevel": "INFO",
  "scannerBeepEnabled": true,
  "scannerVibrateEnabled": true,
  "offlineModeEnabled": true
}
```

Four things about that example are load-bearing:

1. **`netboxBaseUrl` is the server root**, not the API root. The client appends
   `/api/dcim/...` itself. A trailing `/` is tolerated and stripped, because
   `http://host/` + `/api/dcim/devices/` would request `//api/dcim/devices/`,
   which NetBox routes nowhere.
2. **Use a literal IP.** WM6.5 has no mDNS responder and the WinSock path
   resolves through `gethostbyname()`, so a `.local` name simply fails. If you
   want a name, put it in LAN DNS or the device's hosts file.
3. **`activeBackend` matches `netboxInstanceId` exactly.** Naming something that
   is not configured does not silently send scans elsewhere — it falls back to
   HomeBox and journals `BACKEND_UNKNOWN`.
4. **The instance id is `nb-prod`, not `nb`.** Instance ids are free-form, but
   they must be non-empty, at most 31 characters, and free of `:`, `.`, ` ` and
   `]`, because they are parsed back out of every queue record. An id that
   cannot survive a queue record is refused and the default is kept, with
   `BACKEND_ID_INVALID` in the journal.

**NetBox is off until `netboxBaseUrl` is non-empty.** An unconfigured NetBox is
never constructed and never registered, so `activeBackend` cannot select a
server that resolves nothing while the device looks perfectly healthy.

> ⚠️ `Config::Save` rewrites the whole file from the fields it knows, and it
> runs on the first successful HomeBox authentication of every run. Any comment
> key you add to `hb_conf.json` — including the template's `_readme` — is gone
> after that. Keep the annotated copy on the PC.

---

## 📷 The Scan Workflow

Every server call blocks the UI thread, so the resolution order exists to keep
the common case to **one round trip**.

### Phase 0 — local classification, zero requests

The scanned string is classified before anything touches the network:

| Shape | Result |
|-------|--------|
| Empty | Refused |
| A GS1-128 / SSCC-18 carton label | Refused as a carton label, not a device — saves a pointless round trip and a confusing "not found" |
| `NBDEV:<n>`, or a NetBox URL containing `/dcim/devices/<n>/` | Straight to the detail endpoint, no search |
| Anything else | Opaque token — goes to Phase 1 |

A code longer than 128 characters is rejected locally: NetBox caps `name` at 64
and both `serial` and `asset_tag` at 50, so a longer code cannot match anything
and rejecting it saves two round trips on a link where each one freezes the
screen.

### Phase 1 — `?asset_tag__ie=`

```
GET /api/dcim/devices/?format=json&exclude=config_context&limit=5&asset_tag__ie=<code>
```

**The `__ie` suffix is mandatory.** NetBox's `DeviceFilterSet` declares `serial`
with `lookup_expr='iexact'`, but `asset_tag` only appears in `Meta.fields`, so it
gets the default `exact` lookup — which is case-**sensitive** on PostgreSQL.
`?asset_tag=acme-4821` does not match `ACME-4821`, and barcode decoders
routinely differ in case from what was typed into NetBox. `?asset_tag__ie=`
makes it case-insensitive. `asset_tag` is `unique=True`, so this returns 0 or 1
result, never an ambiguous set.

### Phase 2 — `?q=` fallback

```
GET /api/dcim/devices/?format=json&exclude=config_context&limit=5&q=<code>
```

`?q=` searches name, serial, asset_tag, description and comments in one request
— a cheap OR of several exact lookups. But it is an `icontains` **substring**
search, so results are filtered locally down to those matching the scanned code
exactly (case-insensitively) before any of them counts. Without that filter,
scanning `01` would resolve to `sw-core-01`.

### Resolving the result

A zero-result lookup is **`200 OK` with `count: 0` and `results: []`** — never a
404 — so the client reads `count` rather than the status code.

- **0 matches** → "not found", and only when the device could actually reach the
  server. Offline says so instead; concluding "no such device" from a failed
  request is how an operator gets told their gear does not exist.
- **1 match** → straight to the device screen.
- **more than 1** → the disambiguation picker. **The first match is never
  auto-selected.** NetBox does not enforce uniqueness on `serial`, so one scan
  legitimately matching several devices is normal, and silently auto-picking is
  exactly how a technician moves the wrong box. Five matches are retained, which
  is what fits the picker without scrolling; the total the server reported is
  kept separately so the screen can say "5 of 12" rather than pretending there
  were five.

### Two request parameters that are not optional

- **`exclude=config_context` on every device request.** The device viewset uses
  `DeviceWithConfigContextSerializer`, which renders the full merged
  configuration context — arbitrary operator-defined JSON, frequently tens to
  hundreds of kilobytes *per device*. `HttpClient` caps a response at 512 KB, so
  this is a requirement, not a tuning knob.
- **`format=json`.** NetBox's renderer list puts JSON first, so no `Accept`
  header at all still yields JSON, but the browsable HTML API renderer is orders
  of magnitude larger than the JSON and selecting it by accident on a
  memory-constrained device would be catastrophic. The client sends both
  `Accept: application/json` and `?format=json`. It also sends
  `Accept-Encoding: identity`, because WinInet does not auto-decompress gzip on
  all WinCE builds and bandwidth is not the constraint here — code size and heap
  are.

### Writes

Both writes are `PATCH` to the detail endpoint. `PUT` would require the complete
object, so every field the handheld never loaded — `tenant`, `platform`,
`comments`, custom fields, `config_template` — would be sent as absent and
blanked.

**Status** sends the bare string, not the object shape you received:

```
PATCH /api/dcim/devices/231/     {"status": "active"}
```

This read/write asymmetry is real and catches people out: `status` and `face`
come back as `{"value": "...", "label": "..."}` objects, but are written as the
value alone. Foreign keys come back as nested objects and are written as bare
integer ids. The picker offers the seven values `DeviceStatusChoices` ships —
`inventory`, `staged`, `active`, `planned`, `offline`, `failed`,
`decommissioning`, ordered the way a receiving workflow walks them. That list is
the picker's contents, not a validator: a deployment can extend the set through
the `FIELD_CHOICES` config parameter, so a site-defined value obtained some
other way is still sent through.

**A move is one atomic PATCH carrying the complete positional set:**

```
PATCH /api/dcim/devices/231/     {"site": 3, "location": 12, "rack": 7, "position": 42.5, "face": "front"}
```

NetBox validates site / location / rack / position / face against each other — a
rack must belong to the location, which must belong to the site. A move split
into several requests is rejected part way through with a 400 and leaves the
record half moved. Supplying a rack therefore requires supplying site and
location too.

Each field has three states: omitted (server leaves it alone), cleared, or set.
**Clearing is asymmetric**: foreign keys and `position` are cleared with JSON
`null`, but `face` is cleared with an empty string `""`, because its serializer
is `allow_blank=True` with a `''` default and rejects `null`. Unracking a device
is therefore `{"rack": null, "position": null, "face": ""}`.

`position` is a **decimal**, not an integer — NetBox supports half-U mounting, so
`42.5` is a real value and truncating it to `42` would silently move the device.

---

## 📦 Offline Queue Behaviour

Lookups are **never** queued. A queued lookup replayed hours later produces a
record nobody is looking at, and a barcode that does not exist server-side would
fail forever, jamming the queue. Offline, the client says plainly that it cannot
look up. Mutations queue.

### The two transaction types

| Type | Payload |
|------|---------|
| `DEVICE_STATUS` | `STATUS:<deviceId>:<statusValue>` |
| `DEVICE_MOVE` | `MOVE:<json>` |

The move payload is JSON rather than a delimited string because it carries six
optional fields and the JSON parser is already there. Absent keys are omitted:

```json
{"id":"123","site":"3","location":"12","rack":"7","position":"42.5","face":"front"}
```

Both are keyed by a NetBox object id the device learned **while online**, which
is what makes them replay-safe.

### How instance ids route replay

`SyncEngine` tags every queue record with the active backend's instance id,
producing a qualified type token:

```
nb-prod.DEVICE_MOVE
hb.SCAN
```

On replay the engine splits that token on the first `.`, finds the backend with
the matching instance id, and hands it the bare type. This is a data-integrity
requirement, not bookkeeping: **asset tags are unique per NetBox instance, not
globally**, so a move replayed against a different instance would move whatever
device happens to hold that tag there — a corruption that reports itself as a
success.

Switching backends does not drain the queue. The engine holds every *configured*
backend, not just the active one, so work recorded for the backend you just left
still finds its way home. A NetBox whose `netboxBaseUrl` is later removed stays
registered on purpose, for the same reason.

### The three replay outcomes

| Result | Meaning | Effect on the entry |
|--------|---------|---------------------|
| `REPLAY_SENT` | The server accepted the write | Marked synced |
| `REPLAY_RETRY` | Offline, timed out, rejected, or the payload would not parse | **Stays queued** |
| `REPLAY_SKIPPED` | The backend does not implement this transaction type at all | Stays queued, counted separately |

A malformed payload returns `REPLAY_RETRY` and will never succeed. That is
deliberate: discarding an operator's queued work silently is worse than leaving
it visible in the queue view, where it can be inspected and removed on purpose.

### What a SKIPPED entry means

A skipped entry is one the sync **could not address** — it is tagged for a
backend that is not registered, or its payload is malformed, or the backend it
routed to does not implement that type.

Skipped entries are **kept out of the success/failure ratio on purpose**.
Counting them as failures would pin the sync status at "failed" for the life of
the device and make a perfectly healthy queue look broken; counting them as
successes would discard the operator's work. They stay queued, they are counted
separately (`SyncEngine::GetSkippedCount`), and the queue view shows that count
on its own so the entries can be removed deliberately.

In practice a skipped entry means one of:

- `activeBackend` was changed and the old backend's id no longer matches any
  configured instance id — fix the id in `hb_conf.json` and the entries route
  again on the next sync.
- The queue predates this build and carries a type this backend never had.
- A record was hand-edited in the journal file.

---

## 🔧 Troubleshooting

### ❌ Writes return 403 but reads work fine

Two causes, in order of likelihood.

**1. A read-only or IP-restricted token.** NetBox returns 403 both for "no
credentials" and for "this token may not do that". A token with
`write_enabled: false` reads happily and 403s every PATCH; an `allowed_ips`
restriction does the same when the device's apparent source address changes
behind NAT or a WiFi controller. Check both on the token in the NetBox UI.

Note the client does **not** drop its session on a 403 for exactly this reason —
dropping it would break the reads that are still working. It does drop the
session on a 401, which is DRF's answer when a token is present but rejected.

**2. `CSRF Failed: CSRF token missing`.** This is the cookie trap. NetBox lists
DRF's `SessionAuthentication` *before* `TokenAuthentication`. Session auth only
enforces CSRF when it successfully authenticates via a session cookie — a client
that sends no cookies never reaches that code path. But WinInet stores and
replays cookies automatically per session unless told not to, so a stray
`sessionid` picked up from any response makes session auth match first, CSRF
enforcement kicks in, and every unsafe method fails.

The client sets `INTERNET_FLAG_NO_COOKIES` on every request precisely to prevent
this. If you see it anyway, something between the device and NetBox is injecting
a session — check for an authenticating proxy in the path.

### ❌ A scan finds nothing, but the device is definitely in NetBox

**Check the case of the asset tag first.** `?asset_tag=` is case-sensitive;
`?asset_tag__ie=` is not. The client always uses `__ie`, so if you are
reproducing the lookup by hand with `curl`, reproduce it with `__ie` too —
otherwise you will "confirm" a bug that is only in your test command.

Remember also that a zero-result lookup is `200 OK` with `count: 0`, not a 404.
A tool that only checks the status code will report success on a miss.

If the tag is right and the case is right, try the free-text form by hand:

```bash
curl -s -H "Authorization: Token <key>" \
  "http://192.168.20.5/api/dcim/devices/?q=<code>&exclude=config_context&format=json"
```

A hit there but not on the tag lookup means the code is stored in `name` or
`serial` rather than `asset_tag` — which the client's Phase 2 also finds, unless
the code is a strict substring of the stored value, in which case the local
exact-match filter correctly rejects it.

### ❌ 301 Moved Permanently

**A missing trailing slash.** NetBox runs Django's `APPEND_SLASH`, and
`/api/dcim/devices/1` answers `301` with `Location: /api/dcim/devices/1/`. The
documentation says 302; it is a 301.

For writes it is worse than a redirect. Django's `CommonMiddleware` cannot
redirect a body-carrying request without losing the body, so a slash-less PATCH
raises a server error rather than redirecting at all.

The client builds every URL with the trailing slash and sets
`INTERNET_FLAG_NO_AUTO_REDIRECT`, so a 3xx is a visible error rather than a
silent conversion of a PATCH into a GET that appears to succeed while doing
nothing. **If you see a 301, something is rewriting URLs between the device and
NetBox** — check the proxy config for a `rewrite` or a `proxy_pass` that drops
the slash.

### ❌ 406 Not Acceptable: "Invalid version in Accept header"

Something is adding a `version=` parameter to the `Accept` header. NetBox's
`ALLOWED_VERSIONS` contains **only the currently installed version**, so any pin
is a hard failure:

```
$ curl -i -H "Accept: application/json; version=3.5" http://netbox/api/status/
HTTP/1.1 406 Not Acceptable
{"detail":"Invalid version in \"Accept\" header."}
```

The client never sends one, deliberately: there is no compatibility benefit —
NetBox does not serve old API versions — and pinning guarantees the client breaks
on the next server upgrade. If you see a 406, an intermediary is rewriting
`Accept`.

### ❌ Everything fails immediately after configuring NetBox

Work down this list:

1. **`netboxBaseUrl` includes `/api`.** It should not. The client appends the
   full API path itself, so the request becomes `/api/api/dcim/...` and 404s.
2. **The URL is `https://`.** Re-read
   [the transport section](#-transport-https-does-not-work-from-this-device).
   This will not work, and `allowInsecureTls` does not fix it — that key only
   suppresses certificate errors, and the handshake fails before certificates
   are ever considered.
3. **A hostname instead of an IP.** No mDNS on WM6.5; `gethostbyname()` fails.
4. **Wrong `netboxAuthScheme`.** `Token` for a 40-hex-character v1 token,
   `Bearer` for an `nbt_`-prefixed v2 token.
5. **`ALLOWED_HOSTS` in `configuration.py`** does not include the IP or name the
   device is using — NetBox answers 400 for a host it does not recognise.
6. **The nginx `allow`/`deny` block** does not include the scanner VLAN.

The journal is the fastest way to tell these apart: `AUTH_NO_TOKEN` means the
backend has no usable token, `BACKEND_UNKNOWN` means `activeBackend` names
nothing configured, and `BACKEND_ID_INVALID` means an instance id could not
survive a queue record.

### ❌ The queue shows entries that never sync

Check the skipped count in the queue view first — see
[What a SKIPPED entry means](#what-a-skipped-entry-means). Skipped entries are
not failures and are not retried against a backend that cannot address them;
they need either a config fix or a deliberate removal.

An entry that is genuinely failing (not skipped) is being rejected by NetBox.
The most common cause is a move whose positional set became invalid between
being recorded and being replayed — the U is now occupied, or the rack moved
sites. NetBox returns a 400 whose body is keyed by field name with arrays of
strings:

```json
{"position": ["U42 is already occupied or does not have sufficient space."],
 "face": ["Must specify face with rack position."]}
```

That shape is structurally different from every other error, which use a flat
`{"detail": "..."}`. Do not parse the error *strings* — they are `gettext`-wrapped
and localised, so a NetBox serving `Content-Language: de` returns German. Branch
on the status code, and for a 400 on the field key.

---

## 📚 References

- [NetBox REST API Overview](https://netboxlabs.com/docs/netbox/integrations/rest-api/)
- [NetBox REST API Filtering](https://netboxlabs.com/docs/netbox/reference/filtering/)
- [NetBox Security & Authentication Parameters](https://netboxlabs.com/docs/netbox/configuration/security/)
- [NetBox v4.0 Release Notes](https://netboxlabs.com/docs/netbox/release-notes/version-4.0) (`device_role` → `role`)
- [netbox#21906](https://github.com/netbox-community/netbox/issues/21906) — POST/PATCH without a trailing slash
- [WinInet Security (Windows CE 5.0)](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms905663(v=msdn.10))
- [Authentication Services Registry Settings (Windows CE 5.0)](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms925716(v=msdn.10)) — the complete cipher/hash/key-exchange inventory
- [Zebra Developer Portal: HTTPS TLS 1.2](https://developer.zebra.com/thread/30593)
- [Zebra Enterprise Browser TLS/SSL Compliance](https://techdocs.zebra.com/enterprise-browser/1-7/guide/compliance/)
- [caddy#3667](https://github.com/caddyserver/caddy/issues/3667) — no TLS 1.0 support
- [nginx trac #2250](https://trac.nginx.org/nginx/ticket/2250) / [OpenSSL discussion #22752](https://github.com/openssl/openssl/discussions/22752) — SHA-1/MD5 signature strength and `@SECLEVEL=0`
- [Windows Mobile does not support your new SSL certificate](http://jetzemellema.blogspot.com/2015/02/windows-mobile-does-not-support-your.html) — SHA-256 validation
- [Appendix D: Adding a Certificate to the Root Store of a Windows Mobile-based Device](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-phone/cc182241(v=technet.10))

---

<div align="center">

[← Back to Deployment](DEPLOYMENT.md) | [Back to README](../README.md) | [API Notes →](API_NOTES.md)

</div>
