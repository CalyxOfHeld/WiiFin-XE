#!/usr/bin/env python3
"""Publish WiiFin build artifacts into a clean, ready-to-share dist directory."""

import os
import shutil
import sys
import zipfile

def publish_dist():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    src_dir = os.path.dirname(script_dir)
    parent_dir = os.path.dirname(src_dir)

    dol_path = os.path.join(src_dir, 'WiiFin.dol')
    elf_path = os.path.join(src_dir, 'WiiFin.elf')
    meta_path = os.path.join(src_dir, 'apps', 'WiiFin', 'meta.xml')
    icon_path = os.path.join(src_dir, 'apps', 'WiiFin', 'icon.png')
    font_path = os.path.join(src_dir, 'data', 'fonts', 'cjk', 'NotoSansCJK-WiiFin.otf')
    ofl_path  = os.path.join(src_dir, 'data', 'fonts', 'cjk', 'OFL.txt')

    if not os.path.exists(dol_path):
        print(f"Error: {dol_path} does not exist. Build WiiFin first (run make).", file=sys.stderr)
        return 1

    dist_dirs = [
        os.path.join(src_dir, 'dist'),
        os.path.join(parent_dir, 'dist')
    ]

    for dist_dir in dist_dirs:
        apps_wiifin = os.path.join(dist_dir, 'apps', 'WiiFin')
        fonts_dir = os.path.join(apps_wiifin, 'fonts')
        os.makedirs(fonts_dir, exist_ok=True)

        # 1. Copy Homebrew Channel app files (apps/WiiFin/...)
        boot_dol = os.path.join(apps_wiifin, 'boot.dol')
        shutil.copy2(dol_path, boot_dol)

        if os.path.exists(meta_path):
            shutil.copy2(meta_path, os.path.join(apps_wiifin, 'meta.xml'))
        if os.path.exists(icon_path):
            shutil.copy2(icon_path, os.path.join(apps_wiifin, 'icon.png'))
        if os.path.exists(font_path):
            shutil.copy2(font_path, os.path.join(fonts_dir, 'NotoSansCJK-WiiFin.otf'))
        if os.path.exists(ofl_path):
            shutil.copy2(ofl_path, os.path.join(fonts_dir, 'OFL.txt'))

        # 2. Copy standalone binaries directly into dist/ for quick access
        shutil.copy2(dol_path, os.path.join(dist_dir, 'WiiFin.dol'))
        if os.path.exists(elf_path):
            shutil.copy2(elf_path, os.path.join(dist_dir, 'WiiFin.elf'))

        # 3. Create ready-to-share ZIP package (extracts directly to SD root)
        zip_path = os.path.join(dist_dir, 'WiiFin-apps.zip')
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
            for root, _, files in os.walk(os.path.join(dist_dir, 'apps')):
                for file in files:
                    full_p = os.path.join(root, file)
                    rel_p = os.path.relpath(full_p, dist_dir)
                    zf.write(full_p, rel_p)

        print(f"[dist] Published distribution bundle to: {dist_dir}")
        print(f"       - apps/WiiFin/boot.dol ({os.path.getsize(boot_dol):,} bytes)")
        print(f"       - WiiFin-apps.zip      ({os.path.getsize(zip_path):,} bytes)")

    return 0

if __name__ == '__main__':
    sys.exit(publish_dist())
