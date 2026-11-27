# Setting up cloud saves

newlauncher syncs saves with **one** cloud provider: before a game starts it fetches a newer save, after playing
it uploads changes, and on a conflict it asks. Every user needs their own app registration with the provider.
Client ID and secret belong **only** in `sd:/newlauncher/config.ini`, never in the repository or a CI build.
Without a client ID the provider is greyed out in the settings.

Requirement: a build **with TLS** (`python tools/build_wii.py`). The build without TLS hides the cloud features.

## Google Drive

### 1. Project and consent screen

1. Open <https://console.cloud.google.com/> and create a project, for example "newlauncher".
2. **APIs & Services → Library:** search for "Google Drive API" and **enable** it.
3. **APIs & Services → OAuth consent screen** (or "Google Auth Platform"):
   - User type **External**, any app name, your own e-mail as support and developer contact.
   - **Data access / scopes:** add only `https://www.googleapis.com/auth/drive.file`. This scope gives
     newlauncher access only to files it created itself (folder "newlauncher"), not to the rest of your Drive.
4. **Set the publishing status to "In production"** ("Publish app"). In "Testing", refresh tokens expire after
   **7 days** and you have to sign in on the Wii again. Google requires no verification for `drive.file`; the
   sign-in screen at most shows "Google hasn't verified this app", which you can confirm as the owner.

### 2. Create the client ID

1. **APIs & Services → Credentials → Create credentials → OAuth client ID.**
2. Application type **"TVs and Limited Input devices"**.
3. Copy the client ID (`…apps.googleusercontent.com`) and client secret (`GOCSPX-…`). Google does not treat the
   secret as confidential in the device-code flow, but it still does not belong in a repository or screenshot.

### 3. config.ini

Add to `sd:/newlauncher/config.ini`:

```ini
[cloud]
provider = drive
drive_client_id = 1234567890-abc.apps.googleusercontent.com
drive_client_secret = GOCSPX-...

[general]
; optional: shown in the conflict dialog of other devices as the origin of the cloud save (max. 32 characters)
device_name = Living room Wii
```

`provider = drive` can also be chosen in the settings; the client ID must be in the file.

### 4. Sign in on the Wii

1. **Settings → Cloud saves → Provider: Google Drive → Sign in.**
2. On your phone or PC open the shown address (`google.com/device`) and enter the code.
3. Choose the account and allow access. The Wii shows "Signed in".

The tokens are stored encrypted in `sd:/newlauncher/cloud/tokens.bin`. This is only obfuscation: the key is
derived from the Wii's device ID. Whoever has both SD card **and** Wii can reach the "newlauncher" folder, not
the rest of your Drive. **Sign out** deletes the tokens on the SD card. You can revoke access completely at
<https://myaccount.google.com/permissions>.

### What is stored in the cloud

The Drive folder `newlauncher` holds one `<unit>.zip` per save, for example `wii-RMCP.zip` or
`gb-tetris-world-rev-1-d2ff925e.zip`. The ZIP contains the raw save files and `newlauncher-save.ini` (device,
time, content hash) and can be opened on a PC. Locally, newlauncher remembers the last common state in
`sd:/newlauncher/cloud/state.ini`.

## OneDrive

### 1. Register an app in Microsoft Entra

1. Open <https://entra.microsoft.com/> (or <https://portal.azure.com/>) with a Microsoft account.
   **Identity → Applications → App registrations → New registration.**
2. Any name, for example "newlauncher". Supported account types: **"Personal Microsoft accounts only"**
   (newlauncher signs in at the `consumers` endpoint, so work and school accounts do not work). Leave the
   redirect URI empty.
3. **Authentication → Advanced settings → "Allow public client flows"**: set to **Yes** and save. Without this
   switch Microsoft rejects the device code.
4. **API permissions → Add a permission → Microsoft Graph → Delegated permissions:**
   `Files.ReadWrite.AppFolder` and `offline_access`. With `Files.ReadWrite.AppFolder`, newlauncher sees only its
   own app folder. A client secret is **not** needed.
5. **Overview:** copy the **Application (client) ID** (form `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`).

### 2. config.ini

```ini
[cloud]
provider = onedrive
onedrive_client_id = xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

Both providers may be configured; only the one under `provider` is active.

### 3. Sign in on the Wii

1. **Settings → Cloud saves → Provider: OneDrive → Sign in.**
2. On your phone or PC open `microsoft.com/link`, enter the code, sign in with the Microsoft account and agree.

### What is stored in the cloud

OneDrive holds one `<unit>.zip` per save under **Apps → <app name>**, as with Google Drive. Device, time and
content hash are also in the file description (`nl1 hash=… saved=… device=…`); do not edit that line by hand.

**Sign out** deletes the tokens on the SD card. Microsoft offers no revocation of single refresh tokens; you can
revoke access completely at <https://account.live.com/consent/Manage>.

## Dropbox

### 1. Create an app in the Dropbox App Console

1. Open <https://www.dropbox.com/developers/apps> with your Dropbox account → **Create app**.
2. **Scoped access**, access type **App folder** (newlauncher then sees only its own folder). Any name, for
   example "newlauncher-<yourname>" (it must be unique at Dropbox).
3. Tab **Permissions:** tick `files.content.read`, `files.content.write` and `account_info.read`, then press
   **Submit**. Permissions added after a sign-in only apply after signing in again.
4. Tab **Settings → OAuth 2 → Allow public clients (Implicit Grant & PKCE):** keep or set **Allow**.
   newlauncher uses PKCE and needs **no** app secret. Leave the redirect URIs empty.
5. Copy the **App key** (not the app secret).

A new app is in "Development" status at Dropbox and may connect up to 500 accounts, which is enough for personal
use; applying for production is not necessary.

### 2. config.ini

```ini
[cloud]
provider = dropbox
dropbox_app_key = abcdefghijklmno
```

### 3. Pair on the Wii

1. **Settings → Cloud saves → Provider: Dropbox → Sign in.** The pairing screen shows a QR code and below it the
   address `http://<wii-ip>:4301/<token>`.
2. Scan the QR code with your phone (on the **same Wi-Fi** as the Wii). The pairing page opens; tap the button to
   allow access at Dropbox, sign in and agree. Dropbox then shows a code.
3. Copy the code, paste it into the pairing page and submit it. The Wii shows "Signed in".

The pairing page lives only while the screen is open (at most 5 minutes) and accepts exactly **one** submission;
afterwards it reports that the page is no longer active. If Dropbox rejects a code, the screen says so and the
page accepts a new one.

**Without a phone on the Wi-Fi:** **Y** (or **1**) switches to a direct QR code of the Dropbox approval page
(works with any device with Internet access, also over mobile data). Enter the code Dropbox shows with **A** on
the on-screen keyboard (layers abc / ABC / 123 / # via the layer key, up to 128 characters).

The code alone is worthless: it only matches the secret PKCE key that never leaves the Wii. That is why the page
uses plain `http://` on the home network; tokens never pass through the pairing page.

### What is stored in the cloud

Dropbox holds one `<unit>.zip` per save under **Apps → <app name>**. Dropbox has no description field; device,
time and content hash are only in `newlauncher-save.ini` inside the ZIP. The Dropbox `rev` is the revision; an
upload overwrites only if the file is unchanged since the last sync (otherwise newlauncher decides again or
asks).

**Sign out** revokes the token at Dropbox (log `NL_CLOUD_UNLINKED revoke=ok`) and deletes it from the SD card.

## Switching providers and "Sync all"

- **Switching:** change the provider (◀▶ in the "Provider" row), then **Sign in**. The old provider is signed
  out (Google and Dropbox: token revoked). Right after signing in, newlauncher syncs **all** saves of the library
  with the new provider: saves only on the Wii are uploaded, saves only in the cloud are fetched (with a backup
  first). If saves exist on both sides with different content, one dialog asks for all of them: **Keep Wii
  saves**, **Take cloud saves** or **Ask at game start** (default; then each game asks before starting).
- **Sync all** (row in the cloud menu) runs the same sync by hand at any time. While it runs, the row becomes
  "Cancel sync"; the status shows the progress and at the end how many saves were pulled and uploaded.

## Troubleshooting

| Message / log | Cause |
|---|---|
| Provider greyed out or skipped, message about a missing client ID in `config.ini` | `drive_client_id`, `onedrive_client_id` or `dropbox_app_key` missing or empty |
| "This build has no HTTPS", log `NL_CLOUD off notls` | build without TLS |
| "Sign-in expired – please sign in again", log `NL_CLOUD <unit> skip auth` | refresh token invalid (app still in "Testing", access revoked) |
| Log `NL_CLOUD <unit> skip offline` | no network; the game starts without sync, the upload follows later |
| Log `NL_CLOUD_LINK failed` | Google: wrong client ID/secret or wrong application type; Microsoft: "Allow public client flows" off or wrong account type; Dropbox: wrong app key, "Allow public clients" off or pairing expired (5 minutes) |
| "Signed out, revocation not confirmed", log `NL_CLOUD_UNLINKED revoke=fail` | tokens on the SD card are deleted, but the provider did not confirm (offline); revoke access in the account settings if needed |
| "Code rejected", log `NL_CLOUD_LINK badcode` | Dropbox code mistyped, already used or expired; get a new one through the link |
| Phone cannot reach the pairing page, log only `NL_PAIR listen 4301` | phone not on the same Wi-Fi, guest network with client isolation, or mobile data; use **Y** (direct QR) and **A** (keyboard) |
| Pairing page "no longer active", log `NL_PAIR reject` | second submission or screen already closed; start the sign-in again |
| Log `NL_CLOUD_SYNCALL offline` | "Sync all" without network; nothing was changed |

## Testing without a real account

`python tools/dolphin_smoke.py --cloud-fake drive|onedrive|dropbox --cloud-step
link|push|pull|conflict|refresh|offline|syncall|switch|unlink` starts a Drive, Graph or Dropbox fake
(`tools/cloud_fake.py`) over HTTPS with the test CA and checks the flow in Dolphin. The fake IDs and secret exist
only in the tool. For Dropbox, `tools/cloud_smoke.py` plays the phone: it opens the pairing page through
`127.0.0.1:4301` (Dolphin passes Wii sockets through to the PC), follows the link to the fake approval and
submits the code. Dropbox only: `--cloud-step badcode` (a wrong code first, then the right one) and `pairtwice`
(second submission → 410).
