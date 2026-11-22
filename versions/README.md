# MayOS 独立版本源码目录

所有已经发布的 MayOS 版本都保存在 **`versions/<版本号>/`** 中，且保留发布标签对应的原始 Git tree，不把不同版本的源码混合到同一个目录。

- `versions/0.5.2/`：**GitHub 已发布的正式版 v0.5.2 源码**，包含同版本 Live 和离线安装器构建工作流；QEMU 8 项通过，实体机兼容性尚未广泛验证。
- `versions/0.5.0-pre2-dev/`：保留的更早开发源码快照；不是 v0.5.2 的源码。
- `versions/0.5.0-pre2/`：来自 `v0.5.0-pre2` 标签的原始源码。
- `versions/0.5.0-pre1/`、`versions/0.4.1-pre2/` 等：各自标签对应的原始源码。
- `versions/catalog.json`：每份已发布源码的标签名与原始 Git tree SHA，用 `python3 scripts/verify_version_layout.py` 校验。
- `MayOS_0.3.0-pre1`：指向 **`versions/0.5.0-pre2-dev`** 的兼容符号链接，供旧的 Actions、Makefile、脚本继续编译。**不是另一个混装版本源码目录。**

发布版本的源码目录视为只读历史快照；所有未来开发先在新版本目录完成，发布之前严禁以已发布版本目录做原地更新。GitHub Release 附件保持原有 tag，不因目录变化而更名。

## 构建

```sh
python3 scripts/verify_version_layout.py
make -C versions/0.5.0-pre2-dev all
# 旧脚本仍支持：make -C MayOS_0.3.0-pre1 all
```

注意：历史版本是否能够使用当前构建工具链编译，须分别验证。历史目录的 Git tree 匹配发布版本不意味着发行包中包含当时未入库的二进制构建缓存。
