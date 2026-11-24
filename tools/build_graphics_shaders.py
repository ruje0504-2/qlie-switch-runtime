#!/usr/bin/env python3
"""Compile the Vulkan raster and native presentation shaders."""
from pathlib import Path
import struct,subprocess
root=Path(__file__).resolve().parents[1]
for source,symbol in [('raster.comp','rasterSpv'),('present.vert','presentVertexSpv'),('present.frag','presentFragmentSpv')]:
    output=root/'build'/(source+'.spv');output.parent.mkdir(exist_ok=True)
    subprocess.run(['glslangValidator','-V','--target-env','vulkan1.1',str(root/'shaders'/source),'-o',str(output)],check=True)
    data=output.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
    text='// Generated from shaders/'+source+'.\nstatic const uint32_t '+symbol+'[]={\n'+','.join(hex(v) for v in words)+'\n};\n'
    target=root/'src'/('raster_spv.inc' if source=='raster.comp' else source.replace('.','_')+'_spv.inc')
    if not target.exists() or target.read_text()!=text:target.write_text(text)
