# Verify the actual foobar SDK tag writer against a disposable FLAC.
# stdlib only; compare decoded Vorbis tags and exact audio-frame tail.
import sys
from pathlib import Path
def read(path):
    b=Path(path).read_bytes()
    if not b.startswith(b"fLaC") or len(b)>32_000_000: raise ValueError("Invalid fixture")
    p=4; tags=None; stream=None
    while True:
        if p+4>len(b): raise ValueError("Truncated FLAC metadata")
        last=b[p]&128; kind=b[p]&127; n=int.from_bytes(b[p+1:p+4],"big");p+=4
        if p+n>len(b): raise ValueError("Metadata exceeds FLAC file")
        v=b[p:p+n];p+=n
        if kind==0:stream=v
        if kind==4:
            offset=0
            def le32():
                nonlocal offset
                if offset+4>len(v):raise ValueError("Truncated Vorbis field")
                x=int.from_bytes(v[offset:offset+4],"little");offset+=4;return x
            vendor=le32()
            if offset+vendor>len(v):raise ValueError("Invalid vendor")
            offset+=vendor
            total=le32()
            if total>64:raise ValueError("Unbounded tags")
            tags={}
            for _ in range(total):
                n=le32()
                if offset+n>len(v):raise ValueError("Invalid value length")
                item=v[offset:offset+n].decode("utf-8");offset+=n
                name,sep,value=item.partition("=")
                if not sep or name.upper() in tags:raise ValueError("Duplicate tag")
                tags[name.upper()]=value
            if offset!=len(v):raise ValueError("Vorbis trailer unexpected")
        if last:break
    if not tags or not stream or not b[p:]:raise ValueError("Incomplete tagged audio")
    return tags,stream,b[p:]
if len(sys.argv)!=4:raise SystemExit("expected before after mode")
before, bstream, audio=read(sys.argv[1])
after, astream, newaudio=read(sys.argv[2])
if bstream!=astream or audio!=newaudio:
    raise SystemExit("FAIL: Audio frame/STREAMINFO changed")
if sys.argv[3]=="written":
    if before.get("TITLE")!="1000 Years" or before.get("ARTIST")!="Kyau & Albert X Maywave":
        raise SystemExit("Original deterministic FLAC missing")
    expected=dict(before)
    expected["TITLE"]="MMS QA UPDATED (SDK)"
    if after!=expected:
        raise SystemExit("FAIL: Unexpected physical tag mutation "+str(after))
    print("MMS_NATIVE_SDK_PHYSICAL_WRITE=PASS (only TITLE changed; audio and all other tags byte-identical)")
elif sys.argv[3]=="noop":
    if before.get("TITLE")!="MMS QA UPDATED (SDK)" or before!=after:
        raise SystemExit("FAIL: No-op tags changed")
    if Path(sys.argv[1]).read_bytes()!=Path(sys.argv[2]).read_bytes():
        raise SystemExit("FAIL: No-op performed a file rewrite")
    print("MMS_NATIVE_SDK_PHYSICAL_NOOP=PASS (identical file bytes)")
else:raise SystemExit("Unknown QA mode")
