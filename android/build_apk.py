# -*- coding: utf-8 -*-
"""用 JDK + build-tools 手工编译打包 APK（无需 Gradle）。

注意：aapt2 处理不了含中文的路径，所以先把工程拷到纯 ASCII 的临时目录
（默认 C:\\tdapk）里构建，成功后再把 APK 拷回 ..\\每日待办.apk。

用法：python build_apk.py
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
TD = os.path.dirname(ROOT)                       # 每日待办
TOOLS = os.path.join(TD, "_build_tools")
JDK = os.path.join(TOOLS, "jdk")
BT = os.path.join(TOOLS, "sdk", "build-tools", "34.0.0")
PLATFORM = os.path.join(TOOLS, "sdk", "platforms", "android-34", "android.jar")
JAVAC = os.path.join(JDK, "bin", "javac.exe")
KEYTOOL = os.path.join(JDK, "bin", "keytool.exe")
AAPT2 = os.path.join(BT, "aapt2.exe")
D8 = os.path.join(BT, "d8.bat")
ZIPALIGN = os.path.join(BT, "zipalign.exe")
APKSIGNER = os.path.join(BT, "apksigner.bat")

STAGE = r"E:\GitHub\My-codes\VS Code\tdapk"

# 版本号（与 AndroidManifest.xml 的 versionName 保持一致），产物名 每日待办-<VERSION>.apk
VERSION = "1.3"


def run(cmd, cwd=None):
    print("> " + " ".join([os.path.basename(cmd[0])] + cmd[1:]))
    env = dict(os.environ)
    env["JAVA_HOME"] = JDK
    env["PATH"] = os.path.join(JDK, "bin") + os.pathsep + env.get("PATH", "")
    p = subprocess.run(cmd, cwd=cwd or STAGE, shell=False, env=env)
    if p.returncode != 0:
        print("FAILED")
        sys.exit(p.returncode)


def main():
    for p in (JAVAC, AAPT2, D8, ZIPALIGN, APKSIGNER, PLATFORM):
        if not os.path.exists(p):
            print("缺少工具: " + p)
            sys.exit(2)

    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE, ignore_errors=True)
    os.makedirs(os.path.join(STAGE, "classes"), exist_ok=True)
    shutil.copytree(os.path.join(ROOT, "src"), os.path.join(STAGE, "src"))
    shutil.copytree(os.path.join(ROOT, "res"), os.path.join(STAGE, "res"))
    shutil.copy2(os.path.join(ROOT, "AndroidManifest.xml"), os.path.join(STAGE, "AndroidManifest.xml"))

    # aapt2 无法处理中文路径，先拷一份 android.jar 到暂存目录
    plat_ascii = os.path.join(STAGE, "android.jar")
    shutil.copy2(PLATFORM, plat_ascii)

    srcs = []
    for base, _dirs, files in os.walk(os.path.join(STAGE, "src")):
        for f in files:
            if f.endswith(".java"):
                srcs.append(os.path.join(base, f))

    # 1) javac
    run([JAVAC, "-encoding", "UTF-8", "-source", "8", "-target", "8", "-nowarn",
         "-bootclasspath", PLATFORM, "-classpath", PLATFORM,
         "-d", os.path.join(STAGE, "classes")] + srcs)

    # 2) aapt2
    res_zip = os.path.join(STAGE, "res.zip")
    run([AAPT2, "compile", "--dir", os.path.join(STAGE, "res"), "-o", res_zip])
    base_apk = os.path.join(STAGE, "base.apk")
    run([AAPT2, "link", "-o", base_apk,
         "--manifest", os.path.join(STAGE, "AndroidManifest.xml"),
         "-I", plat_ascii, "-R", res_zip, "--auto-add-overlay",
         "--min-sdk-version", "24", "--target-sdk-version", "34"])

    # 3) d8 -> classes.dex（输出到目录，再用 zipfile 塞进 apk）
    classes = []
    for base, _dirs, files in os.walk(os.path.join(STAGE, "classes")):
        for f in files:
            if f.endswith(".class"):
                classes.append(os.path.join(base, f))
    dex_dir = os.path.join(STAGE, "dex")
    os.makedirs(dex_dir, exist_ok=True)
    run([D8, "--release", "--min-api", "24", "--lib", plat_ascii, "--output", dex_dir] + classes)
    dex_file = os.path.join(dex_dir, "classes.dex")
    if not os.path.exists(dex_file):
        print("d8 没有产出 classes.dex")
        sys.exit(3)
    import zipfile
    with zipfile.ZipFile(base_apk, "a", zipfile.ZIP_DEFLATED) as z:
        z.write(dex_file, "classes.dex")

    # 4) zipalign
    aligned = os.path.join(STAGE, "aligned.apk")
    run([ZIPALIGN, "-f", "-p", "4", base_apk, aligned])

    # 5) 签名（keystore 放在工程里并复用，否则每次签名证书都变、无法覆盖安装）
    ks = os.path.join(ROOT, "dailytodo.keystore")
    if not os.path.exists(ks):
        run([KEYTOOL, "-genkeypair", "-keystore", ks, "-alias", "dailytodo",
             "-storepass", "dailytodo", "-keypass", "dailytodo",
             "-dname", "CN=DailyTodo, OU=App, O=DailyTodo, L=CN, S=CN, C=CN",
             "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000"])

    # 产物按版本号命名，旧版本保留（除重大 bug 版本外不删）
    final = os.path.join(TD, "每日待办-" + VERSION + ".apk")
    tmp_final = os.path.join(STAGE, "signed.apk")
    if os.path.exists(final):
        os.remove(final)
    run([APKSIGNER, "sign", "--ks", ks, "--ks-pass", "pass:dailytodo",
         "--key-pass", "pass:dailytodo", "--out", tmp_final, aligned])
    shutil.copy2(tmp_final, final)
    run([APKSIGNER, "verify", final])

    print("\nOK -> %s (%d bytes)" % (final, os.path.getsize(final)))


if __name__ == "__main__":
    main()
