import subprocess
import concurrent.futures
from pathlib import Path

#make directory first
Path("./build/resources/shaders").mkdir(parents=True, exist_ok=True)

#shader compile function
def compile_shader(src_path, dst_path):
    try:
        result = subprocess.run(["glslangValidator", "-V", "-g", "--target-env", "vulkan1.3", src_path, "-o", dst_path], capture_output=True, check=True, text=True)
        print(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"Shader validation failed:\n{e.stdout}")

#then compile shaders
pool = concurrent.futures.ThreadPoolExecutor(max_workers=6)
pool.submit(compile_shader, "./resources/shaders/raytrace.rchit", "../build/resources/shaders/raytrace_chit.spv")
pool.submit(compile_shader, "./resources/shaders/raytrace.rgen", "../build/resources/shaders/raytrace_rgen.spv")
pool.submit(compile_shader, "./resources/shaders/raytrace.rmiss", "../build/resources/shaders/raytrace_rmiss.spv")
pool.submit(compile_shader, "./resources/shaders/raytraceShadow.rmiss", "../build/resources/shaders/raytraceShadow_rmiss.spv")
pool.submit(compile_shader, "./resources/shaders/Default.vert", "../build/resources/shaders/Default_vert.spv")
pool.submit(compile_shader, "./resources/shaders/Default.frag", "../build/resources/shaders/Default_frag.spv")

pool.shutdown(wait=True)