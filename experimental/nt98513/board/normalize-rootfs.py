#!/usr/bin/env python3
"""Retain Buildroot 2024.02.10's unused static-musl loader link outside rootfs."""
import hashlib,json,os,pathlib,sys
config=pathlib.Path(os.environ['BR2_CONFIG']).resolve()
root=pathlib.Path(sys.argv[1]).resolve()
images=pathlib.Path(os.environ['BINARIES_DIR']).resolve()
assert root==config.parent/'target' and images==config.parent/'images'
assert 'BR2_STATIC_LIBS=y' in config.read_text().splitlines()
link=root/'lib'/'ld-musl*'
assert not os.path.lexists(root/'lib/libc.so'), 'unexpected dynamic libc in static target'
retained=images/'unused-static-musl-loader-link'
receipt=images/'static-musl-normalization.json'
if os.path.lexists(link):
 assert link.is_symlink() and os.readlink(link)=='libc.so', 'unexpected loader path/type/target'
 original={'rootfs_path':'/lib/ld-musl*','type':'symlink','target':'libc.so','target_sha256':hashlib.sha256(b'libc.so').hexdigest(),'mode':oct(link.lstat().st_mode)}
 if os.path.lexists(retained):
  assert retained.is_symlink() and os.readlink(retained)=='libc.so', 'unexpected retained path'
  assert json.loads(receipt.read_text())['original']==original
  link.unlink() # Only a newly regenerated identical duplicate; original retained.
 else:
  assert not receipt.exists(), 'receipt exists without retained original'
  images.mkdir(exist_ok=True)
  link.rename(retained)
  receipt.write_text(json.dumps({'original':original,'retained_name':retained.name,'reason':'Pinned Buildroot musl.mk unconditionally creates the wildcard loader link even when shared libc is disabled. Static binaries have no interpreter.','static_libs':True,'dynamic_libc_absent':True},indent=2)+'\n')
else:
 assert retained.is_symlink() and os.readlink(retained)=='libc.so' and receipt.is_file(), 'missing expected original or prior normalization'
print('Static musl placeholder retained outside rootfs')
