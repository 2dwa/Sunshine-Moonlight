# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
from pathlib import Path
import ctypes,hashlib
class API(ctypes.Structure):
 _fields_=[('abi',ctypes.c_uint32),('size',ctypes.c_uint32),('create',ctypes.c_void_p),('process',ctypes.c_void_p),('flush',ctypes.c_void_p),('destroy',ctypes.c_void_p)]
import json
repo=Path(__file__).resolve().parent.parent
config=json.loads((repo/'build/environment.json').read_text(encoding='utf-8-sig'))
base=(repo/'build'/config['sunshineBase']/'tools/hdr_enhanced').resolve()
for folder,dll,export in [('nvidia_rtx_video','foundation_rtx_video_adapter.dll','foundation_truehdr_adapter_get_api'),('nvidia_dlssnr','foundation_dlssnr_adapter.dll','foundation_dlssnr_adapter_get_api')]:
 p=base/folder/dll;lib=ctypes.WinDLL(str(p));get=getattr(lib,export);get.argtypes=[ctypes.c_uint32];get.restype=ctypes.POINTER(API)
 a=get(1);assert a and a.contents.abi==1 and a.contents.size==ctypes.sizeof(API)
 assert all(getattr(a.contents,k) for k in ('create','process','flush','destroy'));assert not get(999)
 print('PASS',dll,'ABI 1, size',a.contents.size,'SHA256',hashlib.file_digest(p.open('rb'),'sha256').hexdigest())
