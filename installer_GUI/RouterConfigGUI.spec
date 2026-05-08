# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['router_config_gui.py'],
    pathex=[],
    binaries=[],
    datas=[('C:\\Users\\DELL\\Desktop\\frontop\\installer_gui\\images\\frontop_logo.jpg', 'images'), ('C:\\Users\\DELL\\Desktop\\frontop\\installer_gui\\assets\\.env', 'assets'), ('C:\\Users\\DELL\\Desktop\\frontop\\installer_gui\\assets\\credentials.json', 'assets'), ('C:\\Users\\DELL\\Desktop\\frontop\\installer_gui\\assets\\sensor-setup_1.0.1-2_mips_24kc.ipk', 'assets')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='RouterConfigGUI',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
