import ctypes
import os

# 1. 定义 Filter 结构体 (必须与 Bridge.cpp 一致)
class CFilter(ctypes.Structure):
    _fields_ = [
        ("nameContains", ctypes.c_char_p),
        ("pathContains", ctypes.c_char_p),
        ("type", ctypes.c_int),
        ("minSize", ctypes.c_ulonglong),
        ("maxSize", ctypes.c_ulonglong),
        ("startTime", ctypes.c_longlong),
        ("targetUid", ctypes.c_int)
    ]

# 2. 加载库
lib_path = os.path.abspath("./build/libcore.so")
lib = ctypes.cdll.LoadLibrary(lib_path)

# 3. 【重点】设置函数参数类型
# int C_PackWithFilter(src, pck, pwd, encMode, filter*, compMode)
# 现在是 6 个参数！最后一个是 int (compMode)
lib.C_PackWithFilter.argtypes = [
    ctypes.c_char_p,        # src
    ctypes.c_char_p,        # dest
    ctypes.c_char_p,        # pwd
    ctypes.c_int,           # encMode
    ctypes.POINTER(CFilter),# filter
    ctypes.c_int            # compMode <--- 新增的！
]

# 设置 Unpack 参数类型
lib.C_Unpack.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]

# 4. 准备测试数据 (造一个重复字符很多的文件，测试 RLE 效果)
os.system("rm -rf rle_test_src rle_out") # 清理旧数据
os.system("mkdir -p rle_test_src")

# 写入 1000 个 'A'。如果不压缩占 1000 字节，RLE压缩后应该极小。
content = b"A" * 1000
with open("rle_test_src/big_file.txt", "wb") as f:
    f.write(content)

print(f"[Init] Created file with 1000 'A's. Size: {os.path.getsize('rle_test_src/big_file.txt')} bytes")

# 5. 执行打包 (开启 RLE)
src = b"./rle_test_src"
pck = b"./compressed.pck"
pwd = b"" # 不加密
enc_mode = 0 # None
comp_mode = 1 # 1 = RLE (我们刚写的)

print("-" * 30)
print("Running Pack with RLE Compression (compMode=1)...")

# 传参：filter 传 None (空指针)，compMode 传 1
result = lib.C_PackWithFilter(src, pck, pwd, enc_mode, None, comp_mode)

if result == 1:
    print("Pack Success!")
    # 验证文件大小
    pck_size = os.path.getsize(pck)
    print(f"Packed File Size: {pck_size} bytes")

    if pck_size < 500:
        print("✅ PASS: File size significantly reduced! RLE is working.")
    else:
        print("❌ FAIL: File size did not decrease much.")
else:
    print("❌ Pack Failed.")
    exit(1)

# 6. 执行解包 (验证还原)
print("-" * 30)
print("Running Unpack...")
dest = b"./rle_out"
lib.C_Unpack(pck, dest, pwd)

# 验证内容
restored_file = "rle_out/big_file.txt"
if os.path.exists(restored_file):
    with open(restored_file, "rb") as f:
        new_content = f.read()

    if new_content == content:
        print(f"✅ PASS: Content matches perfectly ({len(new_content)} bytes).")
    else:
        print("❌ FAIL: Content mismatch!")
else:
    print("❌ FAIL: Restored file not found.")