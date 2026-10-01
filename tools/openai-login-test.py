#!/usr/bin/env python3
# Prueba en el Mac el flujo que hace el panel para leer los limites de Codex
# de una cuenta de ChatGPT: login por codigo de dispositivo (como
# `codex login --device-auth`), lectura de /backend-api/wham/usage y una
# renovacion del token. No guarda nada en disco y no imprime ningun token.
# Con --hola, ademas manda un "hola" minimo a Codex (lo que abre la ventana
# de 5h) y compara el uso antes y despues.
import base64, json, sys, time, urllib.error, urllib.parse, urllib.request

AUTH = 'https://auth.openai.com'
CLIENT_ID = 'app_EMoamEEZ73f0CkXaXp7hrann'
UA = 'Pixelario/1.0 (ESP32-S3)'


def call(method, url, body=None, form=False, headers=None):
    h = {'User-Agent': UA, 'Accept': 'application/json', **(headers or {})}
    data = None
    if body is not None:
        if form:
            data = urllib.parse.urlencode(body).encode()
            h['Content-Type'] = 'application/x-www-form-urlencoded'
        else:
            data = json.dumps(body).encode()
            h['Content-Type'] = 'application/json'
    req = urllib.request.Request(url, data=data, method=method, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors='replace')


def jwt_payload(tok):
    part = tok.split('.')[1]
    return json.loads(base64.urlsafe_b64decode(part + '=' * (-len(part) % 4)))


def redact(o):
    if isinstance(o, dict):
        return {k: ('<%d chars>' % len(v) if isinstance(v, str) and ('token' in k or k in ('code_verifier', 'authorization_code', 'code_challenge', 'oai_is')) else redact(v)) for k, v in o.items()}
    if isinstance(o, list):
        return [redact(x) for x in o]
    return o


st, b = call('POST', AUTH + '/api/accounts/deviceauth/usercode', {'client_id': CLIENT_ID})
uc = json.loads(b)
code = uc.get('user_code') or uc.get('usercode')
print(f'1) usercode HTTP {st}: keys={sorted(uc)} interval={uc.get("interval")!r} expires_at={uc.get("expires_at")}')
print(f'\n   >>> Abre {AUTH}/codex/device e introduce el codigo:  {code}\n', flush=True)

interval = int(str(uc.get('interval', '5')).strip() or 5)
deadline = time.time() + 15 * 60
while True:
    st, b = call('POST', AUTH + '/api/accounts/deviceauth/token', {'device_auth_id': uc['device_auth_id'], 'user_code': code})
    if st == 200:
        tok = json.loads(b)
        print(f'2) deviceauth/token HTTP 200: keys={sorted(tok)}', flush=True)
        break
    if st in (403, 404) and time.time() < deadline:
        time.sleep(interval)
        continue
    sys.exit(f'2) deviceauth/token HTTP {st}: {b[:300]}')

st, b = call('POST', AUTH + '/oauth/token', {
    'grant_type': 'authorization_code', 'client_id': CLIENT_ID, 'code': tok['authorization_code'],
    'redirect_uri': AUTH + '/deviceauth/callback', 'code_verifier': tok['code_verifier']}, form=True)
if st != 200:
    sys.exit(f'3) oauth/token HTTP {st}: {b[:300]}')
t = json.loads(b)
print(f'3) oauth/token HTTP 200: {json.dumps(redact(t))}  (respuesta {len(b)} bytes)')
idp, atp = jwt_payload(t['id_token']), jwt_payload(t['access_token'])
auth_claim = idp.get('https://api.openai.com/auth', {})
acct = auth_claim.get('chatgpt_account_id')
print(f'   id_token: exp en {(idp.get("exp", 0) - time.time()) / 3600:.1f} h, claim auth keys={sorted(auth_claim)}')
print(f'   plan={auth_claim.get("chatgpt_plan_type")} account_id={"si" if acct else "NO"} email={"si" if idp.get("email") else "no"}')
print(f'   access_token: {len(t["access_token"])} chars, exp en {(atp.get("exp", 0) - time.time()) / 3600:.1f} h')
print(f'   refresh_token: {len(t.get("refresh_token", ""))} chars')

for label, extra in (('con ChatGPT-Account-Id', {'ChatGPT-Account-Id': acct} if acct else {}), ('sin ChatGPT-Account-Id', {})):
    st, b = call('GET', 'https://chatgpt.com/backend-api/wham/usage', headers={'Authorization': 'Bearer ' + t['access_token'], **extra})
    print(f'4) wham/usage {label}: HTTP {st}, {len(b)} bytes')
    if st == 200:
        print(json.dumps(redact(json.loads(b)), indent=1))
        break
    print('   ' + b[:300])

st, b = call('POST', AUTH + '/oauth/token', {'grant_type': 'refresh_token', 'client_id': CLIENT_ID, 'refresh_token': t['refresh_token']})
r = json.loads(b) if b.startswith('{') else {}
print(f'5) refresh HTTP {st}: keys={sorted(r)} refresh_token nuevo={"si" if r.get("refresh_token") and r["refresh_token"] != t["refresh_token"] else "no"}')
if st == 200:
    print(f'   access_token nuevo: exp en {(jwt_payload(r["access_token"]).get("exp", 0) - time.time()) / 3600:.1f} h')

if '--hola' in sys.argv:
    acc = t['access_token'] if st != 200 else r['access_token']
    hdr = {'Authorization': 'Bearer ' + acc, **({'ChatGPT-Account-Id': acct} if acct else {})}
    base = 'https://chatgpt.com/backend-api/codex'
    st, b = call('GET', base + '/models?client_version=0.99.0', headers=hdr)
    slugs = []
    try:
        slugs = [m.get('slug') for m in json.loads(b).get('models', [])]
    except Exception:
        pass
    print(f'6) codex/models HTTP {st}: {slugs[:12] if slugs else b[:200]}')
    def window():
        st, b = call('GET', 'https://chatgpt.com/backend-api/wham/usage', headers=hdr)
        w = json.loads(b).get('rate_limit', {}).get('primary_window', {}) if st == 200 else {}
        return w.get('used_percent'), w.get('reset_after_seconds')
    print(f'   uso 5h antes: used={window()}')
    for model in (slugs or ['gpt-5-codex'])[:4]:
        body = {'model': model, 'stream': True, 'instructions': 'Responde con una sola palabra.',
                'input': [{'type': 'message', 'role': 'user', 'content': [{'type': 'input_text', 'text': 'hola'}]}],
                'tool_choice': 'auto', 'parallel_tool_calls': False, 'store': False, 'include': []}
        req = urllib.request.Request(base + '/responses', data=json.dumps(body).encode(), method='POST',
            headers={**hdr, 'User-Agent': UA, 'Content-Type': 'application/json', 'Accept': 'text/event-stream'})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                events = [l.decode().strip() for l in resp if l.startswith(b'event:')]
                print(f'7) responses model={model}: HTTP {resp.status}, eventos={sorted(set(events))[:8]}')
                break
        except urllib.error.HTTPError as e:
            print(f'7) responses model={model}: HTTP {e.code} {e.read()[:300]}')
    time.sleep(3)
    print(f'   uso 5h despues: used={window()}')
