import os
import io
import json
import argparse
import shutil
import requests
import zipfile
import sys

# depends
# pip install requests
#
# libs-config.json
#
# fetch from master
# "url": "https://github.com/nonstd-lite/any-lite"
#
# fetch from tag 0.3.0
# "url": "https://github.com/nonstd-lite/any-lite@0.3.0"
#
# fetch from zip
# "url": "https://github.com/nonstd-lite/any-lite/archive/refs/heads/master.zip"

# High-speed mirrors
DOMESTIC_MIRRORS = [
    "https://gh.llkk.cc/",
    "https://mirror.ghproxy.com/",
    "https://ghproxy.net/"
]

def fix_github_url(url):
    url = url.strip().rstrip('/')
    if url.endswith('.zip'):
        return [url]
    if '@' in url:
        base_url, ver = url.split('@', 1)
        base_url = base_url.rstrip('/')
        # Strictly try v+tag first, then original tag
        tags = []
        tags.append('v' + ver)  
        tags.append(ver) 
        return [f"{base_url}/archive/refs/tags/{t}.zip" for t in tags]
    return [
        f"{url}/archive/refs/heads/master.zip",
        f"{url}/archive/refs/heads/main.zip"
    ]

def download_and_extract(task, target_base_path):
    input_url = task.get("url", "Unknown")
    files_map = task.get("files", {})
    
    print(f"\n>> Target: {input_url}")
    potential_urls = fix_github_url(input_url)
    headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'}

    for download_url in potential_urls:
        short_name = download_url.split('/')[-1]
        for mirror in DOMESTIC_MIRRORS:
            proxy_url = f"{mirror}{download_url}"
            sys.stdout.write(f"   [TRY] {short_name} @ {mirror.split('//')[1]}... ")
            sys.stdout.flush()
            
            try:
                # Fast probe (3s timeout)
                with requests.get(proxy_url, headers=headers, stream=True, timeout=3, allow_redirects=True) as r:
                    if r.status_code != 200:
                        sys.stdout.write("404\n")
                        continue
                    
                    sys.stdout.write("Found!\n")
                    # Start Download
                    file_buffer = io.BytesIO()
                    total_size = int(r.headers.get('content-length', 0))
                    downloaded = 0
                    
                    for chunk in r.iter_content(chunk_size=1024 * 256):
                        if chunk:
                            file_buffer.write(chunk)
                            downloaded += len(chunk)
                            if total_size > 0:
                                p = int(100 * downloaded / total_size)
                                sys.stdout.write(f"\r   [DL] Progress: {p}% ({downloaded}/{total_size} bytes)")
                            else:
                                sys.stdout.write(f"\r   [DL] Downloaded: {downloaded} bytes")
                            sys.stdout.flush()
                    
                    # Extraction
                    print(f"\n   [EXT] Extracting files...")
                    with zipfile.ZipFile(file_buffer) as z:
                        all_names = z.namelist()
                        archive_root = all_names[0].split('/')[0]
                        for src, dest in files_map.items():
                            clean_src = src.replace('./', '').rstrip('*').strip('/')
                            zip_src = f"{archive_root}/{clean_src}".replace("//", "/")
                            
                            found_any = False
                            for m in all_names:
                                if m.startswith(zip_src) and not m.endswith('/'):
                                    if src.endswith(('*', '/')):
                                        rel = os.path.relpath(m, zip_src)
                                        final = os.path.normpath(os.path.join(target_base_path, dest, rel))
                                    else:
                                        final = os.path.normpath(os.path.join(target_base_path, dest))
                                    
                                    os.makedirs(os.path.dirname(final), exist_ok=True)
                                    with z.open(m) as s_f, open(final, "wb") as t_f:
                                        shutil.copyfileobj(s_f, t_f)
                                    found_any = True
                            if found_any: print(f"   [OK] {src} -> {dest}")
                    return True
            except Exception:
                sys.stdout.write("Timeout/Error\n")
                continue
    print(f"   [ERROR] Failed to fetch: {input_url}")
    return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--config", default="libs-config.json")
    parser.add_argument("-o", "--output", default="./")
    args = parser.parse_args()

    if not os.path.exists(args.config):
        print(f"FATAL: Config '{args.config}' not found.")
        return

    try:
        with open(args.config, 'r', encoding='utf-8') as f:
            tasks = json.load(f)
    except Exception as e:
        print(f"FATAL: JSON Error - {e}")
        return

    print("="*60)
    print(f"STARTING: Fetching {len(tasks)} libraries...")
    print("="*60)
    
    success_count = 0
    for task in tasks:
        if download_and_extract(task, args.output):
            success_count += 1
            
    print("\n" + "="*60)
    print(f"SUMMARY: {success_count}/{len(tasks)} succeeded.")
    print("="*60)

if __name__ == "__main__":
    main()