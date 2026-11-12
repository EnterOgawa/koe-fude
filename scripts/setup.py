"""製品buildの固定依存だけを準備します。Python 3.12以上の開発用です。

Copyright (c) 2026 Shunji Ogawa. MIT License.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOSTS = {
    "github.com", "codeload.github.com", "release-assets.githubusercontent.com",
    "objects.githubusercontent.com",
    "gitlab.com", "raw.githubusercontent.com",
    "developer.download.nvidia.com",
}


def check_url(url):
    """固定したHTTPS配布hostだけを許可します。"""
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.hostname not in HOSTS or parsed.username or parsed.password:
        raise ValueError("Download URL is outside the approved HTTPS hosts")
    if parsed.port not in (None, 443):
        raise ValueError("Unexpected HTTPS port")


class CheckedRedirect(urllib.request.HTTPRedirectHandler):
    """初期URLと同じhost allowlistを全redirectにも適用します。"""
    max_redirections = 5

    def redirect_request(self, request, response, code, message, headers, new_url):
        check_url(new_url)
        return super().redirect_request(request, response, code, message, headers, new_url)


def valid(file, size, digest):
    """既存fileのsizeとSHA-256をblock単位で確認します。"""
    if not file.is_file() or file.is_symlink() or file.stat().st_size != size:
        return False
    with file.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() == digest


def download(url, size, digest, destination):
    """一時fileを検証してから置き換え、有効な既存fileを失敗時も保持します。"""
    if valid(destination, size, digest):
        return
    check_url(url)
    if not isinstance(size, int) or not 0 < size <= 4 * 1024**3 or len(digest) != 64:
        raise ValueError("Invalid download manifest")
    destination.parent.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(CheckedRedirect())
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix="koefude-download-", delete=False) as output:
            temporary = pathlib.Path(output.name)
            request = urllib.request.Request(url, headers={"User-Agent": "KoeFude-setup/0.1.0"})
            with opener.open(request, timeout=60) as response:
                check_url(response.url)
                count = 0
                checksum = hashlib.sha256()
                while block := response.read(1024 * 1024):
                    count += len(block)
                    if count > size:
                        raise ValueError("Download exceeded its fixed size")
                    output.write(block)
                    checksum.update(block)
            if count != size or checksum.hexdigest() != digest:
                raise ValueError("Download size or SHA-256 mismatch")
            output.flush()
            os.fsync(output.fileno())
        temporary.replace(destination)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def relative_file(name):
    """archive/manifestの相対pathをWindowsでの逸脱も含め検査します。"""
    path = pathlib.PurePosixPath(name)
    if not name or path.is_absolute() or any(part in ("..", "") for part in path.parts):
        raise ValueError("Unsafe archive path")
    if "\\" in name or ":" in name:
        raise ValueError("Nonportable archive path")
    return path


def unpack_dependency(archive_file, definition, destination):
    """検証済みarchiveから通常fileだけを一時directoryへ展開します。"""
    if destination.exists():
        # 既存checkoutを上書きしない。全通常fileをarchiveの内容と照合する。
        verify_tree(archive_file, definition, destination)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix="koefude-unpack-") as staging:
        temporary = pathlib.Path(staging) / "content"
        temporary.mkdir()
        for path, size, source in dependency_files(archive_file, definition):
            target = temporary.joinpath(*path.parts[1:])
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as output:
                shutil.copyfileobj(source, output, 1024 * 1024)
            if target.stat().st_size != size:
                raise ValueError('Incomplete dependency archive member')
        temporary.rename(destination)


def dependency_files(archive_file, definition):
    """固定root・件数・総容量を検証した通常fileのstreamだけを順番に借用します。"""
    limit = definition.get('expanded_limit', 512 * 1024**2)
    if not 0 < limit <= 4 * 1024**3:
        raise ValueError('Invalid dependency expansion bound')
    total, count = 0, 0
    is_zip = definition.get('archive_format', 'tar') == 'zip'
    with (zipfile.ZipFile(archive_file) if is_zip else tarfile.open(archive_file)) as archive:
        for member in (archive.infolist() if is_zip else archive):
            path = relative_file(member.filename if is_zip else member.name)
            if path.parts[0] != definition['archive_root']:
                raise ValueError('Unexpected archive root')
            if member.is_dir() if is_zip else member.isdir():
                continue
            if is_zip:
                if (member.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Archive contains a symbolic link')
            elif not member.isfile():
                if definition['id'] == 'whisper.cpp' and member.issym():
                    continue
                raise ValueError('Archive contains a nonregular file')
            size = member.file_size if is_zip else member.size
            total += size
            count += 1
            if len(path.parts) < 2 or count > 50000 or total > limit or size < 0:
                raise ValueError('Expanded dependency exceeds its bound')
            with (archive.open(member) if is_zip else archive.extractfile(member)) as source:
                yield path, size, source


def verify_tree(archive_file, definition, destination):
    """既存依存が固定archiveの通常fileと一致することを検証します。"""
    for path, size, source in dependency_files(archive_file, definition):
        target = destination.joinpath(*path.parts[1:])
        if not target.is_file() or target.is_symlink():
            raise ValueError('Existing dependency is not a regular file')
        # source checkoutの小さいtextだけに従来のCRLF許容を残す。
        if size <= 4 * 1024**2 and definition['id'] in ('whisper.cpp', 'eigen'):
            expected, actual = source.read(), target.read_bytes()
            matches = actual == expected or actual.replace(b'\r\n', b'\n') == expected
        else:
            checksum = hashlib.sha256()
            read_size = 0
            while block := source.read(1024 * 1024):
                checksum.update(block)
                read_size += len(block)
            matches = read_size == size and valid(target, size, checksum.hexdigest())
        if not matches:
            raise ValueError(f"Existing dependency differs: {definition['id']} / {path.name}")


CPU_IDS = ("kaldi-native-fbank", "kissfft", "whisper.cpp", "sherpa-onnx", "eigen", "onnxruntime-headers")


PINNED_LINE_ENDINGS = (
    ("ggml/src/ggml-vulkan/ggml-vulkan.cpp", "c73e8f7980cd416f7fd30860c43f85e4ecacb7db84c0bf057cbb505aaf90142f", "f1043f819ed330c7f3e4d65c76918ef32fcc11830753cc4a7ac9b56a045c0e00"),
    ("src/whisper.cpp", "dc70e14a08119b58ed2a7c7c2919f87b120897f75648002202cce58d8e09191f", "362eb497c8b38d2af5e32f0ff0d338d79759fcab33bfe6d3a6f5437eea200b89"),
    ("include/whisper.h", "a7d19f7feb5be52426628ff07e0602de28a30dc4312d0d0603e1e536753f76dd", "43a82a692dccfa8b42cb3f1b5c80ee9d468c2b093a135ad27c11ffd760e07f2e"),
    ("ggml/include/ggml.h", "725ca1d9670770f7ee7e1f736983ce332c4f9f27999a5472b61002ff1fa78ea5", "c8e29a587d77a25b859c3653ed4e2766090da2aa2b9df565373e24b02cc61376"),
    ("ggml/include/ggml-cpu.h", "316279e004cdeb8e6ef78599acb602bf79a8abdf897fed9fd1914808c1518c6e", "696a54474002227a7f73b967b643055d6829853464244a8d3f4fe2e7d0b85abe"),
    ("LICENSE", "94f29bbed6a22c35b992c5c6ebf0e7c92f13b836b90f36f461c9cf2f0f1d010d", "bcd8ec749126d45cb06737d0690295d73df4b6e7e194205bcf91190368f27285"),
)


def normalize_pinned_sources():
    """原archiveの既知6fileだけを既存CMake pinの改行へそろえ、未知内容は変更しません。"""
    for name, source_sha, expected_sha in PINNED_LINE_ENDINGS:
        path = ROOT / "third_party/whisper.cpp" / name
        if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= 4 * 1024**2:
            raise ValueError("Pinned source is not a bounded regular file: " + name)
        original = path.read_bytes()
        actual_sha = hashlib.sha256(original).hexdigest()
        if actual_sha == expected_sha:
            continue
        if actual_sha != source_sha:
            raise ValueError("Pinned source preimage mismatch: " + name)
        converted = original.replace(b"\n", b"\r\n")
        if hashlib.sha256(converted).hexdigest() != expected_sha:
            raise ValueError("Pinned source line ending mismatch: " + name)
        path.write_bytes(converted)


def prepare(cpu=False, cache_root=None):
    """固定lockだけから依存を準備し、cache指定時はnetworkへ接続しません。"""
    manifest = json.loads((ROOT / "dependencies.lock.json").read_text("utf-8"))
    for item in manifest["dependencies"]:
        if cpu and item["id"] not in CPU_IDS:
            continue
        print("Preparing " + item["id"], flush=True)
        relative_file(item["directory"])
        if "files" in item:
            assets = [(asset, ROOT / "third_party" / item["directory"] / relative_file(asset["file"]))
                      for asset in item["files"]]
        else:
            assets = [(item, ROOT / "work/downloads" / (item["id"] + ".tar"))]
        for asset, target in assets:
            if cache_root:
                source = pathlib.Path(cache_root) / target.relative_to(ROOT)
                if not valid(source, asset["size"], asset["sha256"]):
                    raise ValueError("Cached dependency pin mismatch: " + item["id"])
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists():
                    shutil.copyfile(source, target)
                if not valid(target, asset["size"], asset["sha256"]):
                    raise ValueError("Prepared dependency pin mismatch: " + item["id"])
            else:
                download(asset["url"], asset["size"], asset["sha256"], target)
        if "files" not in item:
            unpack_dependency(assets[0][1], item, ROOT / "third_party" / item["directory"])
    normalize_pinned_sources()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu", action="store_true", help="CPU buildに必要な6依存だけを準備")
    parser.add_argument("--dependencies", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--cache-root", type=pathlib.Path, help="検証済み依存のあるlocal source root")
    args = parser.parse_args()
    prepare(args.cpu, args.cache_root)
