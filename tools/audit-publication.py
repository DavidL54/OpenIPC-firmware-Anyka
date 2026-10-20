#!/usr/bin/env python3
"""Read-only audit of the source handoff. No network, builds, tests or deployment."""
from pathlib import Path
import hashlib, json, re, sys
ROOT=Path(__file__).resolve().parent.parent
errors=[]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def manifest(path,base):
    count=0
    for row in path.read_text().splitlines():
        if not row or row.startswith('#'):continue
        expected,name=row.split(maxsplit=1);name=name.lstrip('*');p=(base/name).resolve()
        if not p.is_relative_to(base.resolve()) or not p.is_file() or digest(p)!=expected:
            errors.append('Manifest mismatch: '+name)
        count+=1
    return count
checked=manifest(ROOT/'manifests/PUBLICATION.sha256',ROOT)
source=json.loads((ROOT/'manifests/source-export.json').read_text())
for entry in source['files']:
    p=ROOT/entry['export']
    if not p.is_file() or digest(p)!=entry['export_sha256']:errors.append('Source export mismatch: '+entry['export'])
manifest(ROOT/'native-gateway/vendor/SHA256SUMS',ROOT/'native-gateway/vendor')
blocked={'.bin','.so','.ko','.o','.sqf','.squashfs','.jffs2','.uim','.dtb','.cpio','.pyc','.tar','.zip','.h264','.mp4','.pcm','.mp3'}
private={'web-access.json','wpa_supplicant.conf','authorized_keys','id_rsa','id_ed25519','root-shadow.hash','ptz-presets.json'}
secret=[re.compile(r'^-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----\s*$',re.M),re.compile(r'^\s*psk\s*=\s*"[^"\n]{8,}"',re.M),re.compile(r'"(?:digest_ha1|password_hash)"\s*:\s*"(?:[0-9a-f]{32}|\$2[aby]\$[^"\n]+)"')]
files=[]
for p in sorted(ROOT.rglob('*')):
    rel=p.relative_to(ROOT)
    if any(x in {'.git','.github','__pycache__'} for x in rel.parts):continue
    if p.is_symlink():errors.append('Unexpected symlink: '+str(rel));continue
    if not p.is_file():continue
    files.append(p)
    if p.suffix.lower() in blocked or p.name in private or '.so.' in p.name:errors.append('Binary/private input: '+str(rel))
    b=p.read_bytes()
    if b.startswith((b'\x7fELF',b'hsqs',b'\x1f\x8b')):errors.append('Firmware/archive magic: '+str(rel))
    try:text=b.decode('utf-8')
    except UnicodeDecodeError:continue
    if p.suffix=='.json':
        try:json.loads(text)
        except json.JSONDecodeError as e:errors.append('Invalid JSON '+str(rel)+': '+str(e))
    if not rel.is_relative_to(Path('native-gateway/vendor')):
        if any(x.search(text) for x in secret):errors.append('Potential secret value: '+str(rel))
        if any(x in text for x in ['/home/'+'david','10.'+'0.0.','home.'+'davidleroy.fr']):errors.append('Local identity: '+str(rel))
    if p.suffix=='.md':
        for target in re.findall(r'\[[^\]\n]+\]\(([^)\n]+)\)',text):
            target=target.split(' "',1)[0].strip('<>')
            if re.match(r'[a-zA-Z][\w+.-]*:',target) or target.startswith('#'):continue
            target=target.split('#',1)[0]
            if target and not (p.parent/target).exists():errors.append('Broken Markdown link '+str(rel)+' -> '+target)
board=json.loads((ROOT/'hardware/board.json').read_text());end=0
for part in board['flash']['partitions']:
    if part['offset']!=end:errors.append('Partition gap or overlap')
    end=part['offset']+part['size']
if end!=board['flash']['bytes']:errors.append('Partition sizes do not fill chip')
if errors:print(json.dumps({'status':'failed','errors':errors},indent=2));sys.exit(1)
print(json.dumps({'status':'passed','scope':'Source/publication audit only; no camera/unit/build testing','manifest_entries':checked,'source_provenance_entries':len(source['files']),'files_inspected':len(files),'documentation_chapters':len(list((ROOT/'docs').glob('*.md')))-1},indent=2))
