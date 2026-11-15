# win-panvk-probe (M1)

Windows 11 ARM64 用户态探针：验证 DroidVM 的通用 virtio WDDM 2.0 KMD
（`virtio_wddm_rs.sys`）能否承载 Mali kbase native-context（host
`context_type=0x4b424153`，即 `"KBAS"`）的 kbv RPC。

探针只做 D3DKMT 用户态调用，不安装/不加载驱动、不启停 VM、不修改客机配置。
驱动与 host crosvm 需由主流程预先准备好。

## 探针做什么

1. `D3DKMTEnumAdapters2` 枚举适配器，`D3DKMTQueryAdapterInfo(UMDRIVERPRIVATE)`
   读取 26B `AdapterInfo`：LUID、tag、capset_mask、supports_3d、has_shmem。
2. 按 LUID `D3DKMTOpenAdapterFromLuid` → `D3DKMTCreateDevice` →
   `D3DKMTCreateContext`（node 0 / Graphics，ClientHint=Vulkan）。
3. 查询 DRM capset（id=6），打印 `wire_format_version`、`context_type`、
   `version_major/minor/patchlevel` 和原始 u32 前缀。
4. `ContextInit` escape：`capset_id=DRM(6)`、`num_rings=1`。
5. 创建 `blob_id=0`、`HOST3D|MAPPABLE`、`0x4000` 字节的 ring blob，`BlobMap`
   取 CPU 地址，读取 host 写入的 `vdrm_shmem.rsp_mem_offset`（期望 8）。
6. 通过 ExecBuffer escape（ring 0）提交 `kbv_req`：
   - `cmd=9`：GPU props，先取总长度，再按 1024B 分块读取；
   - `cmd=10`：CSIF，期望 24B。
   每次提交后轮询 `vdrm_shmem.seqno`（有界超时），再从 `rsp_mem` 读 `kbv_rsp`。
7. 默认同一进程连续做 2 轮 connect/teardown，专门覆盖 shmem/BAR offset
   复用后的 stale 页问题。

所有原始值同时打印：KMD 返回的 NTSTATUS、capset u32、KMD 句柄、virtio
resource id、shmem 偏移、`kbv_rsp` 的 status/len/size/value。

## 构建（GitHub Actions）

Workflow：`.github/workflows/build.yml`，`windows-latest`：

- VS2026 ARM64 工具链（`VC.Tools.ARM64`，`vcvarsall.bat arm64`）；
- clang-cl 20，显式 `--target=aarch64-pc-windows-msvc`，`/MT` 静态 CRT；
- 仅使用 Windows SDK 10.0.26100 自带的 `shared/d3dkmthk.h`、`shared/d3dukmdt.h`，
  **不需要 WDK**；
- 产物 `vdrm-probe-arm64`：`vdrm-probe.exe`、`vdrm-probe.pdb`、`SHA256SUMS.txt`。

下载：

```sh
gh run download <run-id> -R collegeming/win-panvk-probe -n vdrm-probe-arm64 -D out
```

仓库为私有仓库；需已登录的 `gh`。不要在任何输出里打印 token。

本地复现（已安装 VS ARM64 工具链 + LLVM 的机器）：

```bat
call "<VS>\VC\Auxiliary\Build\vcvarsall.bat" arm64
"C:\Program Files\LLVM\bin\clang-cl.exe" /nologo /std:c11 /O2 /Zi /W3 /MT ^
  --target=aarch64-pc-windows-msvc ^
  /DUMDF_USING_NTSTATUS /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
  /Isrc\compat /Isrc\third_party\vdrm ^
  src\probe_main.c src\third_party\vdrm\vdrm.c src\third_party\vdrm\vdrm_wddm.c ^
  /Fe:vdrm-probe.exe /link /MACHINE:ARM64 /SUBSYSTEM:CONSOLE
```

## 运行

在 DroidVM Windows 11 ARM64 客机内（驱动已安装、host crosvm 已用
`--gpu context-types=virgl2:drm` 启动）：

```bat
vdrm-probe.exe
```

可选参数：

```bat
vdrm-probe.exe --rounds 2 --timeout-ms 10000
```

- `--rounds N`：同进程 connect/teardown 轮数，默认 2，范围 1..16。
- `--timeout-ms N`：每次 RPC 等待 shmem seqno 的超时，默认 10000，可用
  `VDRM_PROBE_TIMEOUT_MS` 覆盖。
- 探针默认设置 `VDRM_WDDM_DEBUG=1`；escape 级原始日志在 stderr，结构化摘要
  在 stdout。建议同时重定向：`vdrm-probe.exe > probe.txt 2>&1`。
- 进程级 stale 复测：`vdrm-probe.exe && vdrm-probe.exe`（每轮内部已做两轮）。

## 期望输出（关键行）

```text
[vdrm-wddm] EnumAdapters2(count): status=0x00000000 num_adapters=N
[vdrm-wddm] adapter[0]: ... tag=0x464e495041444156("VADAPINF") capset_mask=... supports_3d=1 has_shmem=1 match=1
[vdrm-wddm] OpenAdapterFromLuid ...: status=0x00000000
[vdrm-wddm] CreateDevice: status=0x00000000
[vdrm-wddm] capset id=6: status=0x00000000 raw[0..5]=0x00000002 ... 0x4b424153 ...
[vdrm-wddm] capset6 decoded: wire_format_version=2 ... context_type=0x4b424153 ("KBAS")
[vdrm-wddm] ContextInit capset_id=6 num_rings=1: status=0x00000000
[vdrm-wddm] CreateContext ...: status=0x00000000
[vdrm-wddm] ring CreateAllocation: blob_id=0 size=16384 ... kmt=0x... res_id=...
[vdrm-wddm] BlobMap ... ptr=0x...
[vdrm-wddm] ring ready: ... rsp_mem_offset=8 (host must write 8)
=== round 1/connect ===
[stage 2] DRM capset id=6
  wire_format_version    = 2 (expected 2)
  context_type           = 0x4b424153 ("KBAS") (expected 0x4b424153 "KBAS")
  CAPSET CRITERION OK
[stage 3] shared ring ...
  shmem->seqno            = 0 (fresh blob must be 0)
  shmem->rsp_mem_offset   = 8 (expected 8)
  STALE-PAGE CRITERION OK (rsp_mem_offset=8, seqno=0)
[stage 4] kbv RPC cmd=9 (GPU props: size query)
  rx: len=1048 status=0 handle=0 size=0 value=781
  props total size = 781 (expected 781)
[stage 5] kbv RPC cmd=10 (CSIF info)
  rx: len=1048 status=0 handle=0 size=24 value=0
  CSIF CRITERION OK (returned 24 bytes)
=== summary ===
RESULT: PASS
```

`props` 总长度以 781 为期望值；按 M1 判据，非 0 且 `status=0` 也接受，探针会
打印实际值。CSIF 必须为 24B。shmem 必须是新建页：`rsp_mem_offset=8` 且初始
`seqno=0`；若第二轮仍看到旧的 seqno 或偏移，说明 host/KMD 复用了未重新初始化
的页，探针判 FAIL（seqno 等待使用严格相等，避免旧值“超前”造成假通过）。

## 失败判读

| 现象 | 含义 / 下一步 |
| --- | --- |
| `no virtio WDDM adapter found` | 驱动未安装/未启动，或 AdapterInfo tag 不符；先确认 `virtio_wddm_rs.sys` 已加载且适配器枚举可见。 |
| `capset id=6 escape failed` | KMD 不支持该 escape 或 host 未提供 DRM capset；检查 host crosvm `context-types=virgl2:drm`。 |
| `context_type = 0x00000001` 等 | host 回退到 MSM/其他后端；确认 host 打开 `VKR_TEST_KBASE_NATIVE=1` 且 `/dev/mali0` 可用。 |
| `ContextInit ... failed` | host 未创建 Mali virgl 上下文；检查 host 端 mali_kbase_renderer 日志。 |
| `ring CreateAllocation failed` | KMD CREATE_BLOB 路径失败；检查 KMD 版本是否含 blob_id=0/shmem 支持。 |
| `ring rsp_mem_offset invalid` | host 未写 8，或 shmem 映射是 stale 页；这也是 stale 复测的核心判据。 |
| `TIMEOUT waiting for shmem seqno` | ccmd 未被 host 处理（ContextInit 顺序/提交路径问题），或 KMD 未把 ExecBuffer 送到 control queue。 |
| `props size query failed: status=-...` | host kbase `KBASE_IOCTL_GET_GPUPROPS` 失败，可对照同一负数 errno。 |
| `CSIF size mismatch` | host CSIF ioctl 失败或返回布局不符。 |

退出码：全部判据通过为 0，否则 1；参数错误为 2。

## 已知边界与风险

- 探针只覆盖 ring 0 的 kbv 控制 RPC，不覆盖 GPU 提交、CSF queue、内存分配
  （cmd 11/12/14）等后续里程碑。
- 依赖 host 端 `mali_kbase_renderer` 的 `cmd=9/10` 实现与 shmem blob 布局；
  host 代码若变化，`kbv_req` 1056B / `kbv_rsp` 1048B 需同步。
- KMD 的 ring blob 创建/BlobMap 依赖 host 通用 memfd 路径（未启用 drm2kgsl
  arena）。若 host 开了 `--pre-alloc` arena，KMD 可能返回
  `IO_DEVICE_ERROR`，不在本探针范围内。
- 两轮 connect 已覆盖同进程的偏移复用；跨进程复测建议再连续运行两次 exe。
- 探针 exe 未签名；若客机启用了 HVCI/WDAC 或 Smart App Control，需按现有测试
  流程允许该二进制运行，不要为此关闭系统安全策略。
- 探针不修改系统、不安装驱动、不启动 VM。

## 源码来源与许可证

见 `NOTICE.md`。核心 transport 来自 Mesa vdrm（MIT）与 DroidVM 的
`vdrm_wddm.c`（MIT）；`virtio_wddm_uapi.h` 来自 virtio-d3d11（AGPL-3.0），
仅随本私有实验仓库使用，不用于再分发。探针自身与 compat 层为 MIT。
