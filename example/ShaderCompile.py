import subprocess
import concurrent.futures
import sys
from pathlib import Path

#src and dst absolute paths
src = sys.argv[1]
dst = sys.argv[2]

#make output directory
Path(dst).mkdir(parents=True, exist_ok=True)

#shader compile function
def compile_shader(src_name, dst_name):
    try:
        result = subprocess.run(["glslangValidator", "-V", "-g", "--target-env", "vulkan1.3", src + src_name, "-o", dst + dst_name], capture_output=True, check=True, text=True)
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"Shader validation failed:\n {e.stderr}\n {e.stdout}\n Source path: {src + src_name}\n Destination Path: {dst + dst_name}\n")

#then compile shaders
pool = concurrent.futures.ThreadPoolExecutor(max_workers=12)
pool.submit(compile_shader, "raytrace.rchit",       "raytrace_chit.spv")
pool.submit(compile_shader, "raytrace.rgen",        "raytrace_rgen.spv")
pool.submit(compile_shader, "raytrace.rmiss",       "raytrace_rmiss.spv")
pool.submit(compile_shader, "leaf.rchit",           "leaf_chit.spv")
pool.submit(compile_shader, "leaf.rahit",           "leaf_ahit.spv")
pool.submit(compile_shader, "raytraceShadow.rmiss", "raytraceShadow_rmiss.spv")
pool.submit(compile_shader, "Default.vert",         "Default_vert.spv")
pool.submit(compile_shader, "Default.frag",         "Default_frag.spv")
pool.submit(compile_shader, "leaf.frag",            "leaf_frag.spv")
pool.submit(compile_shader, "Quad.vert",            "Quad.spv")
pool.submit(compile_shader, "BufferCopy.frag",      "BufferCopy.spv")
pool.submit(compile_shader, "BasicAnimation.comp", "basic_animation.spv")

pool.shutdown(wait=True)