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
pool = concurrent.futures.ThreadPoolExecutor(max_workers=2)
pool.submit(compile_shader, "./src/PaperRenderer/Shaders/IndirectDrawBuild.comp", "build/resources/shaders/IndirectDrawBuild.spv")
pool.submit(compile_shader, "./src/PaperRenderer/Shaders/TLASInstBuild.comp", "build/resources/shaders/TLASInstBuild.spv")

pool.shutdown(wait=True)