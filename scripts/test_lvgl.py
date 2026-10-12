"""Compile and render the real LVGL device editors using a native TinyCC harness."""
from pathlib import Path
import subprocess
from concurrent.futures import ThreadPoolExecutor
root=Path(__file__).resolve().parent.parent
compiler=root/'.tools/host/tcc/tcc.exe'
lv=root/'managed_components/lvgl__lvgl'
out=root/'.tools/host/lvgl';out.mkdir(exist_ok=True)
(root/'.tools/screenshots').mkdir(exist_ok=True)
flags=['-DLV_KCONFIG_IGNORE','-DLV_CONF_INCLUDE_SIMPLE','-Itests/lvgl_host','-Itests/controller_stubs','-I.tools/esp-idf/components/json/cJSON','-Imain',f'-I{lv}']
sources=sorted(p for p in (lv/'src').rglob('*.c') if p.name!='lv_font_montserrat_14_aligned.c')+[root/'main/font.c',root/'main/station_font.c',root/'main/large_clock_font.c',root/'tests/lvgl_editor_test.c',root/'.tools/esp-idf/components/json/cJSON/cJSON.c']
def compile_source(pair):
    i,p=pair;obj=out/f'{i}.o'
    result=subprocess.run([str(compiler),*flags,'-c',str(p),'-o',str(obj)],cwd=root,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(f'{p.name}: {result.stderr}')
    return obj
with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_source,enumerate(sources)))
binary=root/'.tools/host/lvgl_editor_test.exe'
subprocess.run([str(compiler),*map(str,objects),'-o',str(binary)],cwd=root,check=True)
subprocess.run([str(binary)],cwd=root,check=True)
