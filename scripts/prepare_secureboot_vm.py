"""Create an isolated NEW VirtualBox fixture; never touch an existing VM or host firmware."""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess
import uuid


def signature_list(certificate):
    x509_type = uuid.UUID("a5c059a1-94e4-4aa7-87b5-ab155c2bf072").bytes_le
    owner = uuid.UUID("d8edbad1-d153-4501-b3fe-51310f0bda00").bytes_le
    signature_size = 16 + len(certificate)
    return struct.pack("<16sIII", x509_type, 28 + signature_size, 0, signature_size) + owner + certificate


def prepare(vbox, directory, certificate_path, memory):
    certificate = pathlib.Path(certificate_path).read_bytes()
    if hashlib.sha256(certificate).hexdigest() != "076f1fea90ac29155ebf77c17682f75f1fdd1be196da302dc8461e350a9ae330":
        raise RuntimeError("Expected the official Windows UEFI CA 2023 certificate")
    directory = pathlib.Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    name = "RecoveryDesk-SecureBoot-" + uuid.uuid4().hex[:10]
    if subprocess.run([vbox, "showvminfo", name], capture_output=True).returncode == 0:
        raise RuntimeError("Refusing to modify an existing VM")
    log = directory / "vm-setup.log"

    def call(*args):
        result = subprocess.run([vbox, *map(str, args)], capture_output=True, timeout=60)
        with log.open("ab") as output:
            output.write(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError("VirtualBox failed: " + result.stderr.decode("utf-8", errors="replace"))
        return result.stdout.decode("utf-8", errors="replace")

    call("createvm", "--name", name, "--ostype", "Windows11_64", "--basefolder", directory, "--register")
    call("modifyvm", name, "--firmware", "efi64", "--memory", memory, "--cpus", "2", "--vram", "64",
         "--graphicscontroller", "vboxsvga", "--nic1", "none", "--clipboard-mode", "disabled", "--drag-and-drop", "disabled",
         "--audio-enabled", "off", "--boot1", "dvd", "--boot2", "none", "--boot3", "none", "--boot4", "none",
         "--uart1", "0x3f8", "4", "--uart-mode1", "file", directory / "boot-serial.log")
    call("storagectl", name, "--name", "SATA", "--add", "sata", "--controller", "IntelAhci")
    call("storageattach", name, "--storagectl", "SATA", "--port", "0", "--device", "0", "--type", "dvddrive", "--medium", "emptydrive")
    call("modifynvram", name, "inituefivarstore")
    call("modifynvram", name, "enrollorclpk")
    call("modifynvram", name, "enrollmssignatures")
    db_path = directory / "db-original.esl"
    call("modifynvram", name, "queryvar", "--name=db", "--filename=" + str(db_path))
    # Restrict this disposable fixture to Windows UEFI CA 2023. A 2011-only loader must fail.
    database = signature_list(certificate)
    combined = directory / "db-with-windows-2023.esl"
    combined.write_bytes(database)
    call("modifynvram", name, "changevar", "--name=db", "--filename=" + str(combined))
    call("modifynvram", name, "secureboot", "--enable")
    config = {"schema_version": 1, "name": name, "vbox": str(vbox), "directory": str(directory), "memory_mb": memory,
              "network": "disabled", "host_disks_attached": False, "host_firmware_modified": False,
              "db_trust": "windows_uefi_2023_only",
              "windows_uefi_2023_cert_sha256": hashlib.sha256(certificate).hexdigest(), "guest_boot_validated": False}
    (directory / "vm.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    return config


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--vbox", required=True)
    parser.add_argument("--directory", required=True)
    parser.add_argument("--windows-2023-certificate", required=True)
    parser.add_argument("--memory", type=int, default=1536, choices=(1024, 1536, 2048))
    args = parser.parse_args()
    print(json.dumps(prepare(args.vbox, args.directory, args.windows_2023_certificate, args.memory), indent=2))
